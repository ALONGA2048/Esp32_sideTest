# ESP32 音樂串流專案 (ESP32 Music Streaming Project)

這是一個整合 ESP32 硬體與 Node.js 伺服器的音樂專案。

## 📂 檔案專區說明 (Folder Structure)

本專案分為兩個主要資料夾，功能如下：

### 1. 📂 esp32_Test
* **用途**：給 ESP32 的各種 `.ino` 測試檔案。
* **說明**：此資料夾內包含用於測試硬體功能（如音訊輸出、Wi-Fi 連線等）的 Arduino 程式碼。建議在燒錄主程式前，先使用此處的檔案驗證硬體接線是否正確。

### 2. 📂 musicServer
* **用途**：透過 Node.js 建立的影音串流 Server。
* **操作說明**：**該資料夾內部已附有獨立的說明檔案，請直接進入 `musicServer` 資料夾閱讀，並依照指示啟動伺服器即可。**