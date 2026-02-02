#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include <WebServer.h>

// ================= 硬體定義 =================
#define I2S_DOUT      46
#define I2S_BCLK      4
#define I2S_LRC       5

// ================= Wi-Fi 設定 =================
const char* ssid     = "along's phone";
const char* password = "17802356";

// ================= 全域物件 =================
Audio audio;
WebServer server(80);

// ================= 跨核心溝通變數 =================
// 這些變數用來讓 Core 0 (網頁) 告訴 Core 1 (音樂) 做什麼
volatile bool hasNewCommand = false;
String commandUrl = "";
volatile bool stopCommand = false;

// ================= 輔助函式 =================
void my_audio_info(Audio::msg_t m) {
    // 這裡可以過濾掉煩人的 syncword 錯誤，只印出重要的
    String msg = String(m.msg);
    if (msg.indexOf("syncword") == -1) { 
        Serial.printf("%s: %s\n", m.s, m.msg);
    }
}

void audio_eof_stream(const char* info){
    Serial.print("End of Stream: ");
    Serial.println(info);
    audio.stopSong(); 
}

// ================= 網頁處理 (將在 Core 0 執行) =================
void handleCommand() {
  if (server.hasArg("act")) {
    String act = server.arg("act");
    
    if (act == "play" && server.hasArg("val")) {
        // 收到播放指令，不直接執行，而是設定旗標通知 Core 1
        commandUrl = server.arg("val");
        hasNewCommand = true; 
        Serial.println("[Web] 收到播放請求，已排程...");
    }
    else if (act == "stop") {
        stopCommand = true;
        Serial.println("[Web] 收到停止請求...");
    }
    else if (act == "pause") {
        // 暫停可以直接呼叫
        audio.pauseResume();
        Serial.println("[Web] 暫停/繼續");
    }
    else if (act == "vol_up") {
        int v = audio.getVolume();
        if (v < 21) audio.setVolume(v + 1);
    }
    else if (act == "vol_dn") {
        int v = audio.getVolume();
        if (v > 0) audio.setVolume(v - 1);
    }
  }
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

// 這是專門給 Web Server 用的任務 (Core 0)
void serverTask(void * parameter) {
  while(true) {
    server.handleClient();
    // 稍微讓出資源，保持系統穩定，但回應速度依然很快
    vTaskDelay(5 / portTICK_PERIOD_MS); 
  }
}

// ================= SETUP =================
void setup() {
    delay(1000);
    Serial.begin(115200);

    // 關閉煩人的 Log (如果不想在 IDE 改設定，這裡也可以覆蓋)
    // esp_log_level_set("*", ESP_LOG_NONE); 

    WiFi.begin(ssid, password);
    WiFi.setSleep(false); // 關閉 WiFi 省電模式
    
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("ESP32 IP: "); Serial.println(WiFi.localIP()); 

    // 1. 設定 Web Server 路由
    server.on("/cmd", handleCommand); 
    server.begin();

    // 2. 啟動 Web Server 任務到 Core 0 (獨立運作)
    xTaskCreatePinnedToCore(
      serverTask,   // 任務函式
      "ServerTask", // 任務名稱
      4096,         // 堆疊大小
      NULL,         // 參數
      1,            // 優先權
      NULL,         // 句柄
      0             // 指定跑在 Core 0
    );
    Serial.println("🚀 Web Server 已啟動於 Core 0 (獨立運作)");
     
    // 3. 初始化 Audio (Core 1)
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(8);
    
    // 🌟 關鍵調整：加大緩衝區以解決 "slow stream"
    

    // 放寬連線超時
    audio.setConnectionTimeout(15000, 15000); 
    
    Audio::audio_info_callback = my_audio_info;
}

// ================= LOOP (Core 1 - 專心處理音樂) =================
void loop() {
    // 1. 優先檢查 Web Server (Core 0) 是否傳來新指令
    if (hasNewCommand) {
        Serial.println("[Main] 執行新播放指令，強制重置...");
        
        // 強制停止，打斷任何重連迴圈
        audio.stopSong(); 
        delay(50); 
        
        audio.connecttohost(commandUrl.c_str());
        hasNewCommand = false;
    }

    if (stopCommand) {
        Serial.println("[Main] 執行停止...");
        audio.stopSong();
        stopCommand = false;
    }

    // 2. 處理音訊 (如果卡住，也不會影響 Core 0 的 Web Server)
    audio.loop(); 
    
    // 讓 Core 1 也能稍微喘息
    vTaskDelay(1); 
}