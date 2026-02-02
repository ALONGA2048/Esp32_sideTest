/*
  ESP32 TFT_eSPI RGB Color Test
  功能：輪流顯示 紅 -> 綠 -> 藍 -> 黑，測試螢幕顯示功能
  
  !!注意!!
  在運行此測試檔時請先將user_setup 腳位設定好
*/

#include <TFT_eSPI.h> 
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); 

void setup() {
  Serial.begin(115200);
  Serial.println("Starting RGB Test...");

  //  初始化螢幕
  tft.init();
  
  // 注意要是沒有翻轉螢幕顏色會紅藍色會對調
  tft.invertDisplay(1);

  //  先全黑清屏
  tft.fillScreen(TFT_BLACK);
  
  // 顯示測試文字
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Display OK!");
  delay(1000);
}

void loop() {
  //  紅色測試
  Serial.println("Color: RED");
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE); 
  tft.setCursor(20, 100);
  tft.setTextSize(3);
  tft.print("RED");
  delay(1000);

  // 綠色測試
  Serial.println("Color: GREEN");
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(20, 100);
  tft.print("GREEN");
  delay(1000);

  //  藍色測試
  Serial.println("Color: BLUE");
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 100);
  tft.print("BLUE");
  delay(1000);
}