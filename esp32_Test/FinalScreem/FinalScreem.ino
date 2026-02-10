#include <Arduino.h>
#include <WiFi.h>
#include "Audio.h"
#include <WebServer.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
// ================= 硬體定義 =================
#define I2S_DOUT      46
#define I2S_BCLK      4
#define I2S_LRC       5

// ================= Wi-Fi 設定 =================
//填入你的wifi資料
const char* ssid     = ".....";
const char* password = ".....";

// 在ipconfig 那邊確認啟動server.js 其本機ip為何 (填入http://你的本機ip:3000) port 不要亂改
String nodeServerHost = "........"; 

// ================= 全域物件 =================
Audio audio;
WebServer server(80);
TFT_eSPI tft = TFT_eSPI();

// ================= LVGL 全域變數 =================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[240 * 10];

// UI 物件指標
lv_obj_t * ui_Label_Title;
lv_obj_t * ui_Label_Status;   
lv_obj_t * ui_Btn_Play;
lv_obj_t * ui_Label_Btn_Play;
lv_obj_t * ui_Btn_Stop;
lv_obj_t * ui_Label_Btn_Stop;
lv_obj_t * ui_Slider_Vol;
lv_obj_t * ui_Slider_Seek;
lv_obj_t * ui_Label_Time;
lv_obj_t * ui_TextArea;
lv_obj_t * ui_Keyboard;
lv_obj_t * ui_Page_Main;   
lv_obj_t * ui_Page_QR;      
lv_obj_t * ui_QRCode;       

// ================= 跨核心/狀態溝通變數 =================
volatile bool hasNewCommand = false;
String commandUrl = "";
volatile bool stopCommand = false;
volatile int volumeChangeReq = 0; 
volatile bool needUpdateUI = false; 

// 播放狀態變數
int serverReportedDuration = 0; 
int serverReportedStart = 0; 
String currentTitle = "Waiting for music...";
bool titleChanged = true; 
volatile bool isPlaying = false;
unsigned long musicStartTick = 0; 
unsigned long pauseStartTick = 0; 

// Seek 相關
String currentResourceId = "";
String currentMode = ""; 
String currentBaseUrl = "";
bool isSeeking = false; 

// ================= 輔助函式 =================

//url 轉碼避免空白跟奇異符號錯誤
String urlDecode(String str) {
    String encodedString = str;
    String decodedString = "";
    char c, code0, code1;
    for (int i = 0; i < encodedString.length(); i++) {
        if (encodedString.charAt(i) == '+') {
            decodedString += ' ';
        } else if (encodedString.charAt(i) == '%') {
            code0 = encodedString.charAt(i + 1);
            code1 = encodedString.charAt(i + 2);
            c = (char) (strtol(&code0, NULL, 16) * 16 + strtol(&code1, NULL, 16));
            decodedString += c;
            i += 2;
        } else {
            decodedString += encodedString.charAt(i);
        }
    }
    return decodedString;
}

// 切割youtube網址拿id
String getYouTubeId(String url) {
    url.trim();
    int idLen = 11;
    int idx = -1;
    if (url.indexOf("v=") != -1) idx = url.indexOf("v=") + 2;
    else if (url.indexOf("youtu.be/") != -1) idx = url.indexOf("youtu.be/") + 9;
    else if (url.indexOf("embed/") != -1) idx = url.indexOf("embed/") + 6;
    
    if (idx != -1 && (idx + idLen) <= url.length()) return url.substring(idx, idx + idLen);
    if (url.length() == 11 && url.indexOf(".") == -1 && url.indexOf("/") == -1) return url;
    return "";
}

String formatTime(int s) {
    int m = s / 60;
    int sec = s % 60;
    char buf[10];
    sprintf(buf, "%02d:%02d", m, sec);
    return String(buf);
}


//判別傳入的網址屬於yt,本地,外部哪一類的含式
void playFromInput(String rawInput) {
    rawInput.trim();
    String finalPlayUrl = "";
   
    currentMode = ""; currentResourceId = ""; currentBaseUrl = nodeServerHost;

    String ytId = getYouTubeId(rawInput); // 這是判斷標準 youtube網址用的
    
    //輸入的是標準 YouTube 網址或 ID 
    if (ytId.length() > 0) {
        finalPlayUrl = nodeServerHost + "/yt?id=" + ytId;
        currentMode = "yt"; 
        currentResourceId = ytId;
        Serial.println("[Logic] Mode: YouTube ID (Standard)");
    } 
    // 輸入的是完整網址 
    else if (rawInput.startsWith("http://") || rawInput.startsWith("https://")) {
        finalPlayUrl = rawInput; 

        //  偵測是否為server的 YouTube API 格式
        if (rawInput.indexOf("/yt?id=") != -1) {
            currentMode = "yt";
           
            int idIdx = rawInput.indexOf("id=");
            if (idIdx != -1) {
                int endIdx = rawInput.indexOf("&", idIdx);
                if (endIdx == -1) endIdx = rawInput.length();
                currentResourceId = rawInput.substring(idIdx + 3, endIdx);
            }
            Serial.println("[Logic] Mode: YouTube (Node API)");
        }
        //  偵測是否為server的本地音樂 API 格式
        else if (rawInput.indexOf("/local-music") != -1) {
            currentMode = "local";
            // 抓出檔名
            int fileIdx = rawInput.indexOf("file=");
            if (fileIdx != -1) {
                int endIdx = rawInput.indexOf("&", fileIdx);
                if (endIdx == -1) endIdx = rawInput.length();
                currentResourceId = rawInput.substring(fileIdx + 5, endIdx);
            }
            Serial.println("[Logic] Mode: Local API");
            // 收到http://你的ip:3000/02.mp3 也會轉為local撥放
        }else if (rawInput.startsWith(nodeServerHost) && rawInput.indexOf("/local-music") == -1) {
            
            int lastSlash = rawInput.lastIndexOf('/');
            String fileName = rawInput.substring(lastSlash + 1);

            finalPlayUrl = nodeServerHost + "/local-music?file=" + fileName;
            currentMode = "local";
            currentResourceId = fileName;
            Serial.println("[Logic] Mode: Direct Server File -> Force API");
        }
        else {
            currentMode = "external";
            Serial.println("[Logic] Mode: Direct URL (External)");
        }
    } 
    // 純檔名 (例如 "02.mp3") 因此直接輸入mp3也能seek控制
    else {
        finalPlayUrl = nodeServerHost + "/local-music?file=" + rawInput;
        currentMode = "local"; 
        currentResourceId = rawInput;
        Serial.println("[Logic] Mode: Local Filename (Auto-API)");
    }

    commandUrl = finalPlayUrl;
    hasNewCommand = true;
    
    Serial.print("[Debug] Set URL: ");
    Serial.println(commandUrl);
    Serial.print("[Debug] Set Mode: ");
    Serial.println(currentMode); 
}

// 基本遇不到 audio 在快斷線都會自動重連線
void audio_eof_stream(const char* info) {
    Serial.print("EOF: "); Serial.println(info);
    stopCommand = true; 
}

// ================= LVGL 驅動 =================
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

// 觸控螢幕位置設定
// !!! 注意不位置不要再這裡改 去setUp 541行那邊的觸控數據更改才準確
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY, 600);
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

// ================= LVGL 事件處理 =================

// 撥放按鈕邏輯
static void event_handler_play_btn(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        audio.pauseResume();
        isPlaying = audio.isRunning();
        needUpdateUI = true; // 通知更新狀態標籤

        if (isPlaying) {
             if (pauseStartTick > 0 && musicStartTick > 0) musicStartTick += (millis() - pauseStartTick);
             pauseStartTick = 0;
        } else {
             pauseStartTick = millis();
        }
    }
}

// 終止建邏輯
static void event_handler_stop_btn(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        stopCommand = true; 
    }
}

// 跳轉進度條邏輯
static void event_handler_seek(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * slider = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSING) isSeeking = true;
    else if (code == LV_EVENT_RELEASED) {
        isSeeking = false;
        int targetSec = (int)lv_slider_get_value(slider);
        
        if (currentMode == "external") return;

        if (currentMode == "yt") commandUrl = currentBaseUrl + "/yt?id=" + currentResourceId + "&ss=" + String(targetSec);
        else if (currentMode == "local") commandUrl = currentBaseUrl + "/local-music?file=" + currentResourceId + "&ss=" + String(targetSec);
        
        hasNewCommand = true;
    }
}

// 音量控制邏輯
static void event_handler_volume(lv_event_t * e) {
    int vol_val = (int)lv_slider_get_value(lv_event_get_target(e));
    audio.setVolume(vol_val);
}

// 鍵盤送出邏輯
static void event_handler_keyboard(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY) {
        String inputStr = String(lv_textarea_get_text(ui_TextArea));
        playFromInput(inputStr);
        lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_indev_reset(NULL, ui_TextArea); 
    } else if(code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_indev_reset(NULL, ui_TextArea);
    }
}

//呼叫螢幕小鍵盤
static void event_handler_ta(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

// 切換到 qr 頁面
static void event_handler_goto_qr(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_add_flag(ui_Page_Main, LV_OBJ_FLAG_HIDDEN);  
        lv_obj_clear_flag(ui_Page_QR, LV_OBJ_FLAG_HIDDEN);  
    }
}

// 返回播放頁面
static void event_handler_goto_main(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(ui_Page_Main, LV_OBJ_FLAG_HIDDEN); // 顯示播放頁
        lv_obj_add_flag(ui_Page_QR, LV_OBJ_FLAG_HIDDEN);    // 隱藏 qr 頁
    }
}

//=====================LVGL螢幕排版位置================================
void build_ui() {
    // 設定全螢幕背景顏色
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x202020), 0);

    // 建立兩個頁面容器
    ui_Page_Main = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_Page_Main, 240, 320);
    lv_obj_set_style_bg_opa(ui_Page_Main, 0, 0); 
    lv_obj_set_style_border_width(ui_Page_Main, 0, 0);
    lv_obj_set_scrollbar_mode(ui_Page_Main, LV_SCROLLBAR_MODE_OFF);

    ui_Page_QR = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_Page_QR, 240, 320);
    lv_obj_set_style_bg_color(ui_Page_QR, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(ui_Page_QR, 0, 0);
    lv_obj_set_scrollbar_mode(ui_Page_QR, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(ui_Page_QR, LV_OBJ_FLAG_HIDDEN);

    // 定義共用樣式 
    static lv_style_t style_badge;
    lv_style_init(&style_badge);
    lv_style_set_radius(&style_badge, 5);
    lv_style_set_bg_opa(&style_badge, LV_OPA_COVER);
    lv_style_set_text_color(&style_badge, lv_color_white());
    lv_style_set_pad_all(&style_badge, 5);

    static lv_style_t style_title;
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(0x00f2fe)); 
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);

    // 播放頁面內容 (全部父物件改為 ui_Page_Main)

    // 標題
    ui_Label_Title = lv_label_create(ui_Page_Main);
    lv_obj_set_width(ui_Label_Title, 160); 
    lv_label_set_long_mode(ui_Label_Title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(ui_Label_Title, LV_ALIGN_TOP_LEFT, 10, 0); 
    lv_obj_add_style(ui_Label_Title, &style_title, 0);
    lv_label_set_text(ui_Label_Title, "ESP32 Player"); 

    // 狀態標籤
    ui_Label_Status = lv_label_create(ui_Page_Main);
    lv_obj_add_style(ui_Label_Status, &style_badge, 0);
    lv_obj_align(ui_Label_Status, LV_ALIGN_TOP_MID, 0, 45);
    lv_label_set_text(ui_Label_Status, "WAITING");
    lv_obj_set_style_bg_color(ui_Label_Status, lv_palette_main(LV_PALETTE_GREY), 0);

    // 輸入框
    ui_TextArea = lv_textarea_create(ui_Page_Main);
    lv_obj_set_size(ui_TextArea, 200, 35);
    lv_obj_align(ui_TextArea, LV_ALIGN_TOP_MID, 0, 80);
    lv_textarea_set_placeholder_text(ui_TextArea, "YouTube ID / File...");
    lv_textarea_set_one_line(ui_TextArea, true);
    lv_obj_set_style_bg_color(ui_TextArea, lv_color_hex(0x404040), 0);
    lv_obj_set_style_text_color(ui_TextArea, lv_color_white(), 0);
    lv_obj_add_event_cb(ui_TextArea, event_handler_ta, LV_EVENT_ALL, NULL);

    // 進度條
    ui_Slider_Seek = lv_slider_create(ui_Page_Main);
    lv_obj_set_size(ui_Slider_Seek, 220, 8);
    lv_obj_align(ui_Slider_Seek, LV_ALIGN_CENTER, 0, 10);
    lv_slider_set_range(ui_Slider_Seek, 0, 100);
    lv_obj_add_event_cb(ui_Slider_Seek, event_handler_seek, LV_EVENT_ALL, NULL);

    // 時間文字
    ui_Label_Time = lv_label_create(ui_Page_Main);
    lv_obj_align_to(ui_Label_Time, ui_Slider_Seek, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_set_style_text_color(ui_Label_Time, lv_color_hex(0xaaaaaa), 0);
    lv_label_set_text(ui_Label_Time, "00:00 / 00:00");

    // 播放按鈕
    ui_Btn_Play = lv_btn_create(ui_Page_Main);
    lv_obj_set_size(ui_Btn_Play, 80, 40);
    lv_obj_align(ui_Btn_Play, LV_ALIGN_BOTTOM_MID, 50, -60);
    lv_obj_set_style_bg_color(ui_Btn_Play, lv_color_hex(0x00c6ff), 0);
    lv_obj_add_event_cb(ui_Btn_Play, event_handler_play_btn, LV_EVENT_ALL, NULL);
    ui_Label_Btn_Play = lv_label_create(ui_Btn_Play);
    lv_label_set_text(ui_Label_Btn_Play, "PLAY");
    lv_obj_center(ui_Label_Btn_Play);

    // 停止按鈕
    ui_Btn_Stop = lv_btn_create(ui_Page_Main);
    lv_obj_set_size(ui_Btn_Stop, 80, 40);
    lv_obj_align(ui_Btn_Stop, LV_ALIGN_BOTTOM_MID, -50, -60);
    lv_obj_set_style_bg_color(ui_Btn_Stop, lv_color_hex(0xff6b6b), 0);
    lv_obj_add_event_cb(ui_Btn_Stop, event_handler_stop_btn, LV_EVENT_ALL, NULL);
    ui_Label_Btn_Stop = lv_label_create(ui_Btn_Stop);
    lv_label_set_text(ui_Label_Btn_Stop, "STOP");
    lv_obj_center(ui_Label_Btn_Stop);

    // 音量滑桿
    ui_Slider_Vol = lv_slider_create(ui_Page_Main);
    lv_obj_set_width(ui_Slider_Vol, 180);
    lv_obj_align(ui_Slider_Vol, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_slider_set_range(ui_Slider_Vol, 0, 21);
    lv_slider_set_value(ui_Slider_Vol, 8, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui_Slider_Vol, event_handler_volume, LV_EVENT_VALUE_CHANGED, NULL);

   // Web 按鈕
    lv_obj_t * btn_qr = lv_btn_create(ui_Page_Main);
    lv_obj_set_size(btn_qr, 50, 30);
    lv_obj_align(btn_qr, LV_ALIGN_TOP_RIGHT, 0, 10); 
    lv_obj_set_style_bg_color(btn_qr, lv_palette_main(LV_PALETTE_LIME), 0);
    lv_obj_add_event_cb(btn_qr, event_handler_goto_qr, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * lbl_qr = lv_label_create(btn_qr);
    lv_label_set_text(lbl_qr, "Web"); 
    lv_obj_center(lbl_qr);

    // 鍵盤 (通常維持在螢幕最上層，父物件用 lv_scr_act)
    ui_Keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(ui_Keyboard, ui_TextArea);
    lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui_Keyboard, event_handler_keyboard, LV_EVENT_ALL, NULL);

    // qrcode 頁面內容 
    lv_obj_t * qr_title = lv_label_create(ui_Page_QR);
    lv_label_set_text(qr_title, "Scan to Control");
    lv_obj_set_style_text_color(qr_title, lv_color_white(), 0);
    lv_obj_align(qr_title, LV_ALIGN_TOP_MID, 0, 20);
    
    // 產生qrcode
    String webUrl = nodeServerHost;
    ui_QRCode = lv_qrcode_create(ui_Page_QR, 150, lv_color_hex(0x000000), lv_color_hex(0xffffff));
    lv_qrcode_update(ui_QRCode, webUrl.c_str(), webUrl.length());
    lv_obj_align(ui_QRCode, LV_ALIGN_CENTER, 0, 0);

    // 返回按鈕
    lv_obj_t * btn_back = lv_btn_create(ui_Page_QR);
    lv_obj_set_size(btn_back, 100, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn_back, event_handler_goto_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_center(lbl_back);
}

// ================= Audio Info 音樂標頭處理 =================
// 由於yt及本地音樂seek是直接控制 因此我繞過複雜的音樂資料流傳遞
// 直接用標頭傳遞當前跳轉時間,音樂總長度
void my_audio_info(Audio::msg_t m) {
    String msg = String(m.msg);
    if (msg.indexOf("Title:") != -1) {
        int titleIdx = msg.indexOf("Title:");
        int endIdx = msg.indexOf(";", titleIdx);
        if (endIdx == -1) endIdx = msg.length();
        currentTitle = urlDecode(msg.substring(titleIdx + 6, endIdx));
        titleChanged = true; 
    }
    if (msg.indexOf("TotalSec:") != -1) {
        int totalIdx = msg.indexOf("TotalSec:");
        int endIdx = msg.indexOf(";", totalIdx); 
        if (endIdx == -1) endIdx = msg.length();
        serverReportedDuration = msg.substring(totalIdx + 9, endIdx).toInt();

        int startIdx = msg.indexOf("StartSec:");
        if (startIdx != -1) {
            int endIdx2 = msg.indexOf(";", startIdx);
            if (endIdx2 == -1) endIdx2 = msg.length();
            serverReportedStart = msg.substring(startIdx + 9, endIdx2).toInt();
        }
    }
    if (msg == "stream ready") {
        musicStartTick = millis();
        isPlaying = true;
        needUpdateUI = true;
    }
}

// =================每秒狀態請求=================
// 可以去server網頁看看 他動態更新就是每秒call 這個地方來獲取esp32狀態
void handleStatus() {
    int currentSec = 0;
    if (musicStartTick > 0 && isPlaying) {
         currentSec = (millis() - musicStartTick) / 1000;
    }
    String json = "{";
    json += "\"time\":" + String(currentSec + serverReportedStart) + ","; 
    json += "\"dur\":" + String(serverReportedDuration) + ",";
    json += "\"vol\":" + String(audio.getVolume()) + ",";
    json += "\"isPlay\":" + String(isPlaying ? "true" : "false") + ",";
    json += "\"title\":\"" + currentTitle + "\""; 
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}



//=========處理外部傳入的操作請求===========
//  由於處理音樂的函式庫 對於靜態檔案沒有直接支援seek 
//  因此這部分我直接在內部計時來控制時間流程
//  這裡處理撥放,跳轉,暫停,開始,中止,音量 請查看server內的api獲取各詳細功能
//
void handleCommand() {
  if (server.hasArg("act")) {
    String act = server.arg("act");
    String val = server.arg("val");
    
    if (act == "play") {
      playFromInput(val); 
    } 
    else if (act == "seek") {
       if (currentMode != "external") {
           String seconds = val;
           if (currentMode == "yt") commandUrl = currentBaseUrl + "/yt?id=" + currentResourceId + "&ss=" + seconds;
           else if (currentMode == "local") commandUrl = currentBaseUrl + "/local-music?file=" + currentResourceId + "&ss=" + seconds;
           hasNewCommand = true;
       }
    }
    else if (act == "stop") {
        stopCommand = true;
    }
    else if (act == "pause") {
       audio.pauseResume();
       isPlaying = audio.isRunning();
       needUpdateUI = true;
       
       if (isPlaying) {
             if (pauseStartTick > 0 && musicStartTick > 0) musicStartTick += (millis() - pauseStartTick);
             pauseStartTick = 0;
       } else {
             pauseStartTick = millis();
       }
    }
    else if (act == "vol_up") volumeChangeReq = 1;
    else if (act == "vol_dn") volumeChangeReq = -1;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void serverTask(void * parameter) {
  while(true) {
    server.handleClient();
    vTaskDelay(5 / portTICK_PERIOD_MS); 
  }
}

// ================= SETUP =================
void setup() {
    delay(500);
    Serial.begin(115200);

    tft.begin();
    tft.setRotation(0); 
    // 觸控螢幕參數 要是觸控怪怪的 去tft附的calibar調整
    uint16_t calData[5] = { 401, 3337, 215, 3555, 2 };
    tft.setTouch(calData);
    tft.invertDisplay(1);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240; disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    build_ui(); 

    Serial.println("UI Initialized");

    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    lv_label_set_text(ui_Label_Title, "Connecting WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        lv_timer_handler(); delay(200);
    }
    String ipInfo = "IP: " + WiFi.localIP().toString();
    // 印出目前esp32 的ip 位置
    Serial.println(ipInfo);
    server.on("/cmd", handleCommand); 
    server.on("/status", handleStatus);
    server.begin();
    xTaskCreatePinnedToCore(serverTask, "ServerTask", 4096, NULL, 1, NULL, 0);

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(8);
    audio.setConnectionTimeout(10000, 10000); 
    Audio::audio_info_callback = my_audio_info;
}

// ================= LOOP =================
void loop() {
    audio.loop(); 

    // 處理播放指令
    if (hasNewCommand) {
        audio.stopSong(); 
        delay(50); 
        
        musicStartTick = 0; 
        pauseStartTick = 0;
        serverReportedDuration = 0;
        serverReportedStart = 0;
        
        if (commandUrl.indexOf("&ss=") != -1) {
            int ssIdx = commandUrl.indexOf("&ss=");
            serverReportedStart = commandUrl.substring(ssIdx + 4).toInt();
        }

        audio.connecttohost(commandUrl.c_str());
        hasNewCommand = false;
        
       
        lv_label_set_text(ui_Label_Btn_Play, "PAUSE");
        isPlaying = true;
        needUpdateUI = true;
    }
    
    // 音量
    if (volumeChangeReq != 0) {
        int newVol = audio.getVolume() + volumeChangeReq;
        if (newVol >= 0 && newVol <= 21) {
            audio.setVolume(newVol);
            if (ui_Slider_Vol) lv_slider_set_value(ui_Slider_Vol, newVol, LV_ANIM_ON);
        }
        volumeChangeReq = 0;
    }

    // 停止指令
    if (stopCommand) {
        audio.stopSong();
        stopCommand = false;
        isPlaying = false;
        needUpdateUI = true;
        
        musicStartTick = 0;
        pauseStartTick = 0;
        serverReportedDuration = 0;
        serverReportedStart = 0;
        
        lv_slider_set_value(ui_Slider_Seek, 0, LV_ANIM_ON);
        lv_label_set_text(ui_Label_Time, "00:00 / 00:00");
        Serial.println("Stopped.");
    }

    // 播放結束檢查
    if(isPlaying){
        int currentSec = 0;
        if (musicStartTick > 0) currentSec = (millis() - musicStartTick) / 1000;
        int realPosition = currentSec + serverReportedStart;
        if(serverReportedDuration > 0){
            if (realPosition >= serverReportedDuration) {
                stopCommand = true; 
            }
        }
            // 若覺得時間怪怪的 開啟這個debug
        //Serial.printf("[計時中] 經過: %d秒 / 真實位置: %d / 目標: %d\n", 
        //                currentSec, realPosition, serverReportedDuration);
        
    }
  

    // UI 更新 Loop (每 5ms)
    static uint32_t last_lvgl_tick = 0;
    if (millis() - last_lvgl_tick > 5) {
        last_lvgl_tick = millis();
        
        if (titleChanged) {
            lv_label_set_text(ui_Label_Title, currentTitle.c_str());
            titleChanged = false;
        }
        
        // 更新 UI 狀態：播放按鈕文字 + 狀態標籤顏色
        if (needUpdateUI) {
            if (isPlaying) {
                lv_label_set_text(ui_Label_Btn_Play, "PAUSE");
                
                
                lv_label_set_text(ui_Label_Status, "PLAYING");
                lv_obj_set_style_bg_color(ui_Label_Status, lv_palette_main(LV_PALETTE_GREEN), 0);
            } else {
                lv_label_set_text(ui_Label_Btn_Play, "PLAY");
                
                // 音樂停止(stopCommand歸零變false且playing false)，要區分是 Pause 還是 Stop
                if (serverReportedDuration == 0 && musicStartTick == 0) {
                     // 真正的停止狀態
                     lv_label_set_text(ui_Label_Status, "WAITING");
                     lv_obj_set_style_bg_color(ui_Label_Status, lv_palette_main(LV_PALETTE_GREY), 0);
                } else {
                     // 暫停狀態
                     lv_label_set_text(ui_Label_Status, "PAUSED");
                     lv_obj_set_style_bg_color(ui_Label_Status, lv_palette_main(LV_PALETTE_ORANGE), 0);
                }
            }
            needUpdateUI = false;
        }

        // 更新進度條
        
        if (isPlaying && serverReportedDuration > 0) {
             int currentSec = 0;
             if (musicStartTick > 0) currentSec = (millis() - musicStartTick) / 1000;
             int totalSec = currentSec + serverReportedStart;

             String timeStr = formatTime(totalSec) + " / " + formatTime(serverReportedDuration);
             lv_label_set_text(ui_Label_Time, timeStr.c_str());

             lv_slider_set_range(ui_Slider_Seek, 0, serverReportedDuration);
             if (!isSeeking) {
                 lv_slider_set_value(ui_Slider_Seek, totalSec, LV_ANIM_OFF);
             }
        }
        
        lv_timer_handler(); 
        lv_tick_inc(5); 
    }
}