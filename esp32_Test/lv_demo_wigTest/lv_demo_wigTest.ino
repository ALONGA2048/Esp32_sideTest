#include <lvgl.h>
#include <TFT_eSPI.h>
#include <demos/lv_demos.h>

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10]; 

/* 螢幕刷新區域函式 */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    uint16_t touchX, touchY;

    // 600 是壓力門檻值，可根據靈敏度調整
    bool touched = tft.getTouch(&touchX, &touchY, 600);

    if (!touched) {
        data->state = LV_INDEV_STATE_REL; 
    } else {
        data->state = LV_INDEV_STATE_PR; 
        
        data->point.x = touchX;
        data->point.y = touchY;

        // 偵錯用
        // Serial.printf("Touch: X=%d, Y=%d\n", touchX, touchY);
    }
}
void setup() {
    Serial.begin(115200);
    
    // 初始化螢幕
    tft.begin();
    tft.setRotation(0); 
    
    // 注意!! 更換螢幕方向須重新測試螢幕數據
    uint16_t calData[5] = { 401, 3337, 215, 3555, 2 };
    tft.setTouch(calData);

    // 使顏色調換正常
    tft.invertDisplay(1);

    lv_init();
    
    // 初始化顯示緩衝區
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 320 * 10);

    // 註冊顯示驅動
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* 3. 註冊觸控輸入驅動 */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER; // 滑鼠/觸控筆類型
    indev_drv.read_cb = my_touchpad_read;   // 指定讀取函式
    lv_indev_drv_register(&indev_drv);

    // 指定demo (也可切換成其他demo)
    lv_demo_widgets(); 
    Serial.println("LVGL Widgets Demo Started with Touch support");
}

void loop() {
    lv_timer_handler(); 
    delay(5);
    lv_tick_inc(5); 
}