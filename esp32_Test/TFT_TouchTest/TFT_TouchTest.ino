#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1); // 橫向顯示 (320x240)
  
  // 設定背景為白色
  tft.fillScreen(TFT_WHITE);
  
  // 設定提示文字顏色與大小
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.invertDisplay(1);

  String msg = "Draw anywhere with stylus";
  int16_t xPos = (tft.width() - tft.textWidth(msg)) / 2;
  int16_t yPos = (tft.height() - 8) / 2; 
  
  tft.setCursor(xPos, yPos);
  tft.println(msg);
  
  // 觸控校準參數 (請用Calibar數據 複寫這行)
  uint16_t calData[5] = { 275, 3620, 264, 3532, 1 }; 
  // ---------------------------------------------
  tft.setTouch(calData);
}

void loop() {
  uint16_t x, y;
  
  // getTouch 會偵測觸碰，並自動根據 setRotation 轉換為像素座標
  if (tft.getTouch(&x, &y)) {
    Serial.printf("觸碰座標: x=%d, y=%d\n", x, y);
    
    // 在觸碰位置畫圓點，顏色設為黑色 (TFT_BLACK)
    // 半徑設為 2，增加筆觸感
    tft.fillCircle(x, y, 2, TFT_BLACK); 
  }
}