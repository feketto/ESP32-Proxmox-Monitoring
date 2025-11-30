#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Wallpaper
#include "wallpaper.h" 

// ================= CONFIGURATION =================
const char* ssid     = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// Proxmox IP and Node Name
const char* proxmox_ip = "PROXMOX_LOCAL_IP"; 
const int   proxmox_port = PROXMOX_NETWORK_PORT;
const char* node_name    = "PROXMOX_NODE_NAME";

// API Token (Updated with your new secret)
const char* api_token_id = "PROXMOX_API_TOKEN";
const char* api_secret   = "API_TOKEN_PASSWORD";

// BUTTON PINS (LilyGo T-Display Standard)
#define BUTTON_1 0
#define BUTTON_2 35
#define BACKLIGHT_PIN 4 

// Settings
const int page_auto_cycle_ms = 10000; // Switch page every 10s
const int data_update_ms = 20000;     // Fetch data every 20s




// =================================================

TFT_eSPI tft = TFT_eSPI(); 
WiFiClientSecure client;

// Data Storage
struct NodeStats {
    // Page 1: Resources
    float cpu = 0.0;
    float ram_gb = 0.0;
    float ram_percent = 0.0;
    float disk_percent = 0.0;
    float swap_percent = 0.0;
    uint64_t ram_total_bytes = 1; // Used for GB calculation
    uint64_t disk_total_bytes = 1; // Used for GB calculation
    
    // Page 2: System
    float load_avg[3] = {0.0, 0.0, 0.0};
    uint64_t net_in = 0;
    uint64_t net_out = 0;
    String uptime = "";
    float net_total_usage = 0.0; 

    // Page 3: Guests
    int lxc_running = 0;
    int lxc_total = 0;
    int vm_running = 0;
    int vm_total = 0;

    bool online = false;
};
NodeStats currentStats;

// State Management
int currentPage = 0;
const int totalPages = 4; // Resources, System, Guests, Wallpaper

// Timers
unsigned long lastDataUpdate = 0;
unsigned long lastPageChange = 0;

// --- HELPER FUNCTIONS ---

String formatBytes(uint64_t bytes) {
    if (bytes < 1024) return String((int)bytes) + " B";
    else if (bytes < 1048576) return String((float)bytes / 1024.0, 2) + " KB";
    else if (bytes < 1073741824) return String((float)bytes / 1048576.0, 2) + " MB";
    else return String((float)bytes / 1073741824.0, 2) + " GB";
}

String formatUptime(unsigned long seconds) {
    unsigned long days = seconds / 86400;
    seconds %= 86400;
    unsigned long hours = seconds / 3600;
    seconds %= 3600;
    unsigned long mins = seconds / 60;
    if (days > 0) return String(days) + "d " + String(hours) + "h";
    return String(hours) + "h " + String(mins) + "m";
}

void drawProgressBar(int x, int y, int w, int h, float percentage, uint16_t color) {
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    if(percentage > 1.0) percentage = 1.0;
    if(percentage < 0.0) percentage = 0.0;
    int fillW = (int)((w-2) * percentage);
    tft.fillRect(x+1, y+1, fillW, h-2, color);
    tft.fillRect(x+1+fillW, y+1, (w-2)-fillW, h-2, TFT_BLACK);
}

void connectWiFi() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    tft.drawCentreString("Connecting...", tft.width()/2, 40, 2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(ssid, tft.width()/2, 70, 1);

    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        tft.fillRect(0, 100, tft.width(), 10, TFT_BLACK);
        String dots = "";
        for(int i=0; i<(attempts%20); i++) dots += ".";
        tft.drawCentreString(dots, tft.width()/2, 100, 1);
        attempts++;
        if (attempts > 40) ESP.restart(); 
    }
}

void fetchProxmoxData() {
    client.setInsecure(); 
    HTTPClient http;
    
    String auth = "PVEAPIToken=" + String(api_token_id) + "=" + String(api_secret);
    String baseUrl = "https://" + String(proxmox_ip) + ":" + String(proxmox_port) + "/api2/json/nodes/" + String(node_name);

    // 1. STATUS
    if (http.begin(client, baseUrl + "/status")) {
        http.addHeader("Authorization", auth);
        if (http.GET() > 0) {
            DynamicJsonDocument doc(8192); 
            deserializeJson(doc, http.getString());
            JsonObject data = doc["data"];
            
            currentStats.cpu = data["cpu"] | 0.0;
            
            uint64_t totalMem = data["memory"]["total"].as<uint64_t>();
            uint64_t usedMem = data["memory"]["used"].as<uint64_t>();
            currentStats.ram_total_bytes = totalMem;
            if (totalMem > 0) {
                currentStats.ram_gb = (float)usedMem / 1073741824.0; 
                currentStats.ram_percent = (float)usedMem / (float)totalMem;
            }

            uint64_t totalDisk = data["rootfs"]["total"].as<uint64_t>();
            uint64_t usedDisk = data["rootfs"]["used"].as<uint64_t>();
            currentStats.disk_total_bytes = totalDisk;
            if (totalDisk > 0) currentStats.disk_percent = (float)usedDisk / (float)totalDisk;

            uint64_t totalSwap = data["swap"]["total"].as<uint64_t>();
            uint64_t usedSwap = data["swap"]["used"].as<uint64_t>();
            if (totalSwap > 0) currentStats.swap_percent = (float)usedSwap / (float)totalSwap;

            JsonArray load = data["loadavg"];
            currentStats.load_avg[0] = load[0].as<float>();
            currentStats.load_avg[1] = load[1].as<float>();
            currentStats.load_avg[2] = load[2].as<float>();
            
            uint64_t netIn = data["netin"].as<uint64_t>();
            uint64_t netOut = data["netout"].as<uint64_t>();
            currentStats.net_in = netIn;
            currentStats.net_out = netOut;
            // Simple visual indicator for network activity (based on last 5s)
            currentStats.net_total_usage = (float)(netIn + netOut) / 500000000.0f; // Max out at ~500MB/s total for visual bar

            currentStats.uptime = formatUptime(data["uptime"].as<unsigned long>());
            currentStats.online = true;
        } else {
            currentStats.online = false;
        }
        http.end();
    }

    // 2. LXC COUNTS
    if (http.begin(client, baseUrl + "/lxc")) {
        http.addHeader("Authorization", auth);
        if (http.GET() > 0) {
            DynamicJsonDocument doc(8192);
            deserializeJson(doc, http.getString());
            JsonArray arr = doc["data"].as<JsonArray>();
            currentStats.lxc_total = arr.size();
            currentStats.lxc_running = 0;
            for(JsonObject obj : arr) {
                if (obj["status"] == "running") currentStats.lxc_running++;
            }
        }
        http.end();
    }

    // 3. VM (QEMU) COUNTS
    if (http.begin(client, baseUrl + "/qemu")) {
        http.addHeader("Authorization", auth);
        if (http.GET() > 0) {
            DynamicJsonDocument doc(8192);
            deserializeJson(doc, http.getString());
            JsonArray arr = doc["data"].as<JsonArray>();
            currentStats.vm_total = arr.size();
            currentStats.vm_running = 0;
            for(JsonObject obj : arr) {
                if (obj["status"] == "running") currentStats.vm_running++;
            }
        }
        http.end();
    }
}

void drawHeader(String title, String pageIdx) {
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(5, 2);
    tft.print(title);
    
    if(currentStats.online) tft.fillCircle(130, 9, 4, TFT_GREEN);
    else tft.fillCircle(130, 9, 4, TFT_RED);

    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawRightString(pageIdx, 235, 5, 1);
    tft.drawFastHLine(0, 18, 240, TFT_DARKGREY);
}

// --- PAGE 1: RESOURCES ---
void drawPage1() {
    drawHeader("RESOURCES", "1/4");

    int rowY = 24; int rowH = 28; // Increased row height slightly
    int barX = 35; int barW = 155; int barH = 8; // Slightly thinner bar
    int valX = 238;

    // CPU
    tft.setTextColor(TFT_SILVER); tft.drawString("CPU", 2, rowY, 1);
    uint16_t cpuColor = (currentStats.cpu > 0.8) ? TFT_RED : ((currentStats.cpu > 0.5) ? TFT_YELLOW : TFT_GREEN);
    drawProgressBar(barX, rowY+2, barW, barH, currentStats.cpu, cpuColor);
    tft.setTextColor(TFT_WHITE); 
    tft.drawRightString(String(currentStats.cpu * 100.0, 0) + "%", valX, rowY+2, 1);
    // Node Name (Moved up)
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString(node_name, 35, rowY + barH + 4, 1); // Placed below the bar, starting at barX

    // RAM
    rowY += rowH;
    tft.setTextColor(TFT_SILVER); tft.drawString("RAM", 2, rowY, 1);
    drawProgressBar(barX, rowY+2, barW, barH, currentStats.ram_percent, TFT_SKYBLUE);
    tft.setTextColor(TFT_WHITE); 
    tft.drawRightString(String(currentStats.ram_gb) + "GB", valX, rowY+2, 1);
    // Detail (GB/GB)
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString(formatBytes(currentStats.ram_total_bytes), barX, rowY + barH + 4, 1);


    // DISK
    rowY += rowH;
    tft.setTextColor(TFT_SILVER); tft.drawString("DSK", 2, rowY, 1);
    drawProgressBar(barX, rowY+2, barW, barH, currentStats.disk_percent, TFT_ORANGE);
    tft.setTextColor(TFT_WHITE); 
    tft.drawRightString(String(currentStats.disk_percent * 100.0, 0) + "%", valX, rowY+2, 1);
    // Detail (TB/TB)
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString(formatBytes(currentStats.disk_total_bytes), barX, rowY + barH + 4, 1);


    // SWAP
    rowY += rowH;
    tft.setTextColor(TFT_SILVER); tft.drawString("SWP", 2, rowY, 1);
    drawProgressBar(barX, rowY+2, barW, barH, currentStats.swap_percent, TFT_MAGENTA);
    tft.setTextColor(TFT_WHITE); 
    tft.drawRightString(String(currentStats.swap_percent * 100.0, 0) + "%", valX, rowY+2, 1);
}

// --- PAGE 2: SYSTEM ---
void drawPage2() {
    drawHeader("SYSTEM", "2/4");

    int y = 25; int rowH = 15;
    int col1 = 5; 
    
    // UPTIME
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Uptime:", col1, y, 1);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(currentStats.uptime, col1 + 50, y, 1);

    // NETWORK VISUAL BAR (Horizontal bar shows current usage)
    y += rowH + 5;
    tft.setTextColor(TFT_SILVER);
    tft.drawString("Network Activity", col1, y, 1);
    
    uint16_t netColor = (currentStats.net_total_usage > 0.8) ? TFT_RED : TFT_GREEN;
    drawProgressBar(col1, y + 12, 230, 8, currentStats.net_total_usage, netColor);
    
    // LOAD AVG
    y += rowH + 10;
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Load Average:", col1, y, 1);
    
    y += rowH;
    tft.setTextColor(TFT_SILVER);
    tft.drawString("1m:", col1, y, 1);
    tft.drawString("5m:", col1 + 60, y, 1);
    tft.drawString("15m:", col1 + 120, y, 1);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(String(currentStats.load_avg[0], 2), col1 + 25, y, 1);
    tft.drawString(String(currentStats.load_avg[1], 2), col1 + 85, y, 1);
    tft.drawString(String(currentStats.load_avg[2], 2), col1 + 145, y, 1);
    
    // DATA TOTALS
    y += rowH + 5;
    tft.drawFastHLine(0, y, 240, TFT_DARKGREY);
    y += 5;
    
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Data Totals", col1, y, 1);
    
    y += rowH;
    tft.setTextColor(TFT_SILVER); tft.drawString("Rx (In):", col1, y, 1);
    tft.setTextColor(TFT_GREEN); tft.drawString(formatBytes(currentStats.net_in), col1 + 50, y, 1);
    
    y += rowH;
    tft.setTextColor(TFT_SILVER); tft.drawString("Tx (Out):", col1, y, 1);
    tft.setTextColor(TFT_YELLOW); tft.drawString(formatBytes(currentStats.net_out), col1 + 50, y, 1);
}

// --- PAGE 3: GUESTS ---
void drawPage3() {
    drawHeader("GUESTS", "3/4");
    
    int y = 30;
    int boxW = 80;
    int boxH = 50; 
    int spacing = 55;
    
    // LXC Section (Left Box)
    int lxcX = 15;
    tft.fillRoundRect(lxcX, y, boxW, boxH, 5, TFT_BLUE);
    
    // FIX: Black text, Blue background (for transparency against the box)
    tft.setTextColor(TFT_BLACK, TFT_BLUE);
    tft.drawCentreString("LXC", lxcX + boxW/2, y + 5, 2); 
    
    // LXC Count (Large)
    tft.setTextSize(3); 
    tft.setTextColor(TFT_WHITE, TFT_BLUE); // White text for contrast on Blue background
    tft.drawCentreString(String(currentStats.lxc_running), lxcX + boxW/2, y + 25, 3);
    tft.setTextSize(1);
    
    // VM Section (Right Box)
    int vmX = lxcX + boxW + spacing;
    tft.fillRoundRect(vmX, y, boxW, boxH, 5, TFT_ORANGE);
    
    // FIX: Black text, Orange background (for transparency against the box)
    tft.setTextColor(TFT_BLACK, TFT_ORANGE);
    tft.drawCentreString("VM", vmX + boxW/2, y + 5, 2);
    
    // VM Count (Large)
    tft.setTextSize(3); 
    tft.setTextColor(TFT_BLACK, TFT_ORANGE); // Black text for contrast on Orange background
    tft.drawCentreString(String(currentStats.vm_running), vmX + boxW/2, y + 25, 3);
    tft.setTextSize(1);
    
    // FOOTER - Total Status
    y += boxH + 10;
    tft.setTextColor(TFT_CYAN, TFT_BLACK); // Reset background to black for footer
    tft.drawCentreString("Total Guests", tft.width()/2, y, 2);
    
    y += 20;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String lxcTotalStr = "LXC Total: " + String(currentStats.lxc_total);
    String vmTotalStr = "VM Total: " + String(currentStats.vm_total);
    
    tft.drawCentreString(lxcTotalStr, tft.width()/2, y, 1); 
    tft.drawCentreString(vmTotalStr, tft.width()/2, y + 15, 1); 
}

// --- PAGE 4: WALLPAPER ---
void drawPage4() {
    drawHeader("WALLPAPER", "4/4"); 
    tft.setSwapBytes(true); 
    tft.pushImage(0, 0, 240, 135, wallpaper);
}

void drawCurrentPage() {
    if (currentPage == 0) drawPage1();
    else if (currentPage == 1) drawPage2();
    else if (currentPage == 2) drawPage3();
    else if (currentPage == 3) drawPage4();
}

void setup() {
    Serial.begin(115200);
    
    // Buttons setup: Input Pullup is standard for the LilyGo buttons
    pinMode(BUTTON_1, INPUT_PULLUP);
    pinMode(BUTTON_2, INPUT_PULLUP);
    
    // Force Backlight ON
    pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH); 
    
    // Turn off extra LEDs to save power/annoyance
    pinMode(25, OUTPUT); digitalWrite(25, LOW); 
    pinMode(2, OUTPUT); digitalWrite(2, LOW); 

    tft.init();
    tft.setRotation(1); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true); 

    connectWiFi();
    
    fetchProxmoxData();
    drawCurrentPage();
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Manual Switch
    if (digitalRead(BUTTON_1) == LOW || digitalRead(BUTTON_2) == LOW) {
        currentPage++;
        if (currentPage >= totalPages) currentPage = 0;
        drawCurrentPage();
        delay(200); // Debounce
        lastPageChange = currentMillis; 
    }

    // 2. Auto Switch
    if (currentMillis - lastPageChange > page_auto_cycle_ms) {
        currentPage++;
        if (currentPage >= totalPages) currentPage = 0;
        drawCurrentPage();
        lastPageChange = currentMillis;
    }

    // 3. Data Fetch
    if (currentMillis - lastDataUpdate > data_update_ms) {
        fetchProxmoxData();
        // Redraw only if we are on a data-driven page (1-3)
        if (currentPage < 3) drawCurrentPage(); 
        lastDataUpdate = currentMillis;
    }
}