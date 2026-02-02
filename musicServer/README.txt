
=============啟動server必要配置操作=============

1. 請下載18版本以後的node.js 應用程式
2. 請在powershell 移動到此資料夾內 指令:"cd  C:/你的路徑/這個資料夾"
3. 輸入指令: "npm install" (將安裝所有所需套件)
4. 安裝完成後輸入指令: "node server.js" (啟動server)
5. 開啟: http://你的本機ip:3000 即可看到網頁

=============環境介紹==================
1. 在public資料夾是網頁資料夾透過http://你的本機ip:3000 能找到網頁
2. 在public資料夾能放入音樂檔案直接透過網頁撥放(檔案名稱請盡量不要英文以外的語言LVGL將無法顯示)
2. 在tools資料夾放的是server用到的yt解析跟轉碼工具
3. server.js是server的運行邏輯其他檔案是環境設定請不要更動

=============操作說明==================
前言: 這個server建立目的是為了能直接撥放本地音樂以及直接處理youtube連結來串流撥放音樂
     提供跳轉音樂功能(直接用i2s的內建seek有些撥放來源沒辦法) 想再做更多擴展功能歡迎爆改
網頁操作:
   1. 進入網頁後,請在右上角的IP填入esp32 IP 按下儲存這樣網頁才會連接esp32能動態操作
   2. 網址能輸入三種: yt連結(單一曲目),你在public資料夾丟入的音樂檔案,其他外部音樂檔案(甚至是電台連結) 按下撥放就會開始撥放
   3. 可以點擊上方進度條調整撥放的音樂進度(注意!能調整的的只限本地檔案以及youtube連結)
server api:
	esp32-> server : http://本機ip:3000/yt
	    參數:id=youtube連結 
		參數:ss=跳轉秒數
		能通知server對youtube連結進行處裡並進行串流音樂撥放
	esp32-> server : http://本機ip:3000/local-music
	    參數:file=本機音樂檔案 
		參數:ss=跳轉秒數
		能通知server對本機音樂進行處裡並進行串流音樂撥放
esp32 api:
    網頁-> esp32: http://esp32 ip/cmd
	    參數:act=play
		參數:val=撥放網址 
		接收這個指令來撥放開始撥放音樂
	網頁-> esp32: http://esp32 ip/cmd
	    參數:act=seek
		參數:val=跳轉秒數 
		接收這個來替當前的音樂進行進度跳轉
	網頁-> esp32: http://esp32 ip/cmd
	    參數:act=stop
		接收這個來替當前的音樂暫停
	網頁-> esp32: http://esp32 ip/cmd
	    參數:act=pause
		接收這個來替當前的音樂暫停或繼續撥放
	網頁-> esp32: http://esp32 ip/cmd
	    參數:act=vol_up
		接收這個來替當前的音樂聲音+1
	網頁-> esp32: http://esp32 ip/cmd
	    參數:act=vol_dn
		接收這個來替當前的音樂聲音-1
	網頁-> esp32: http://esp32 ip/status
		接收這個會回傳當前esp32的狀態的json檔案
		數值:	
		  "time": 125,        // 當前播放秒數
		  "dur": 300,         // 總長度秒數
		  "vol": 8,           // 當前音量 (0-21)
		  "isPlay": true,     // 是否正在播放 (true/false)
		  "title": "Song Title" // 歌名 (解碼後的字串)
		  
=============其他==================
操作可能出現的錯誤訊息:
   若在網頁操作沒有任何反應請右鍵->檢查->network查看請求是否有正常發送
   若出現500 error 1.請重開無線基地台 讓server跟esp32 重新連接 2.請確定有填入esp32 ip並按下儲存
   
有任何問題或改進的請寄信到-> 11311117@gm.nttu.edu.tw 專人服務
	




