const express = require('express');
const { spawn, exec } = require('child_process');
const path = require('path');
const cors = require('cors');
const fs = require('fs');
const { PassThrough } = require('stream');
const app = express();
const PORT = 3000;

app.use(cors());

// ===== 路徑設定 (全部指向 tools 資料夾) =====
// 由於我們在 download-binary.js 已經把檔案都搬進去了，這裡直接指名
const TOOLS_DIR = path.join(__dirname, 'tools');
const YT_DLP_PATH = path.join(TOOLS_DIR, 'yt-dlp.exe');
const FFMPEG_PATH = path.join(TOOLS_DIR, 'ffmpeg.exe');
const FFPROBE_PATH = path.join(TOOLS_DIR, 'ffprobe.exe');

// 檢查一下工具是否存在，方便除錯
if (!fs.existsSync(FFMPEG_PATH) || !fs.existsSync(FFPROBE_PATH)) {
    console.warn("警告：在 tools 資料夾中找不到 ffmpeg 或 ffprobe，請確認是否已執行 npm install");
}

// ===== 全域狀態 =====
let currentSharedStream = null;
let currentMetadata = {
    id: null,
    process_ytdlp: null, // 雖然這次改版不太會用到它，但保留結構
    process_ffmpeg: null
};

// ===== 全域狀態 =====
const metaCache = new Map();
const CACHE_DURATION = 3600 * 1000 * 24; 
const fetchingMap = new Map(); 

// 指定pubic 為靜態網站根目錄
app.use(express.static(path.join(__dirname, 'public')));

// youtube連結處理邏輯
app.get('/yt', async (req, res) => { 
    const videoId = req.query.id;
    const seekTime = req.query.ss || "0"; 

    if (!videoId) return res.status(400).end();

    console.log(`\n------------------------------------------------`);
    console.log(`收到 YouTube 請求: ${videoId}, 跳轉: ${seekTime}s`);

    // 檢查快取 
    let cachedMeta = metaCache.get(videoId);
    let now = Date.now();

    if (cachedMeta && (now - cachedMeta.timestamp < CACHE_DURATION)) {
        console.log(`[快取命中]直接使用已知資訊`);
        startStreamPipeline(res, videoId, cachedMeta.duration, seekTime, videoId);
        return;
    }

    //檢查是否已經有別人正在查這首歌 (防止並發競爭)
    if (fetchingMap.has(videoId)) {
        console.log(`[排隊中] 發現已有查詢任務，等待結果...`);
        try {
           
            const totalDuration = await fetchingMap.get(videoId);
            console.log(`[排隊完成] 取得資訊，開始下載`);
            startStreamPipeline(res, videoId, totalDuration, seekTime, videoId);
        } catch (err) {
            console.error('等待查詢失敗:', err);
            res.status(500).send('Fetch error');
        }
        return;
    }

   
    console.log(`[新任務] 無快取，執行 yt-dlp 獲取資訊...`);
    
    // 建立一個 Promise 讓別人可以等
    const fetchPromise = new Promise((resolve, reject) => {
        const infoCmd = `"${YT_DLP_PATH}" --dump-json --no-playlist --skip-download "https://www.youtube.com/watch?v=${videoId}"`;
        
        exec(infoCmd, { maxBuffer: 1024 * 1024 * 10 }, (error, stdout, stderr) => {
            if (error) {
                console.error('yt-dlp Info 錯誤:', stderr);
                reject(error);
                return;
            }
            try {
                const info = JSON.parse(stdout);
                const totalDuration = info.duration;

               
                metaCache.set(videoId, {
                    duration: totalDuration,
                    timestamp: Date.now()
                });

                resolve(totalDuration); 
            } catch (e) {
                console.error('JSON 解析錯誤', e);
                reject(e);
            }
        });
    });

    // 將這個 Promise 存入 Map，讓後面的人排隊
    fetchingMap.set(videoId, fetchPromise);

    try {
        const totalDuration = await fetchPromise;
       
        fetchingMap.delete(videoId); 
        
        startStreamPipeline(res, videoId, totalDuration, seekTime, videoId);
    } catch (error) {
        fetchingMap.delete(videoId);
        res.status(500).send('yt-dlp error');
    }
});
function startStreamPipeline(res, videoId, totalDuration, seekTime, safeTitle) {
    const headerString = `TotalSec:${totalDuration};StartSec:${seekTime};Title:${safeTitle}`;
    
    //禁用超時限制 (防止 Node.js 在長時間播放時主動斷線)
    if (res.socket) res.socket.setTimeout(0); 

    if (!res.headersSent) {
        res.writeHead(200, {
            'Content-Type': 'audio/mpeg',
            'Transfer-Encoding': 'chunked',
            'Connection': 'keep-alive',
            'Cache-Control': 'no-cache',
            'icy-name': headerString
        });
    }

    console.log(`cV 啟動管線下載: yt-dlp -> 緩衝區 -> ffmpeg -> response (跳轉: ${seekTime}s)`);

    // 建立一個 200MB 的緩衝區
    const bufferStream = new PassThrough({ highWaterMark: 200 * 1024 * 1024 });

    //啟動 yt-dlp 參數
    const ytArgs = [
        '-f', 'bestaudio',
        '--no-playlist',
        '-o', '-', 
        '--download-sections', `*${seekTime}-inf`, 
        
        '--force-ipv4', 
        '--retries', '3',
        `https://www.youtube.com/watch?v=${videoId}`
    ];

    const ytProcess = spawn(YT_DLP_PATH, ytArgs, { stdio: ['ignore', 'pipe', 'ignore'] });

    //啟動 FFmpeg 參數
    const ffmpegArgs = [
        '-i', 'pipe:0',
        '-vn',
        '-acodec', 'libmp3lame',
        '-b:a', '128k',
        '-ar', '44100',
        '-ac', '2',
        '-map_metadata', '-1',
        '-f', 'mp3',
        '-'
    ];

    const ffmpegProcess = spawn(FFMPEG_PATH, ffmpegArgs, { stdio: ['pipe', 'pipe', 'pipe'] });

    // =========================================================
    // 連接管線 (加入 bufferStream)
    // =========================================================

    // yt-dlp寫入 -> 緩衝區 
    ytProcess.stdout.pipe(bufferStream);

    // 緩衝區 -> 慢慢餵給 -> ffmpeg
    bufferStream.pipe(ffmpegProcess.stdin);

    // ffmpeg -> response
    ffmpegProcess.stdout.pipe(res);

    // =========================================================
    // 錯誤監聽
    // =========================================================
    const handleError = (origin, err) => {
        if (err.code !== 'EPIPE' && err.code !== 'EOF') {
            // console.error(`${origin} error:`, err.message);
        }
    };

    ytProcess.stdout.on('error', (err) => handleError('yt-dlp', err));
    bufferStream.on('error', (err) => handleError('buffer', err)); 
    ffmpegProcess.stdin.on('error', (err) => handleError('ffmpeg-in', err));
    ffmpegProcess.stdout.on('error', (err) => handleError('ffmpeg-out', err));
    res.on('error', (err) => handleError('response', err));

    ffmpegProcess.stderr.on('data', (data) => {
        const msg = data.toString();
       
        if (msg.includes('Error') && !msg.includes('Pipe') && !msg.includes('Orphaned')) {
             // console.error('FFmpeg Log:', msg); 
        }
    });

    // 清理機制
    const cleanup = () => {
        try {
            if (ytProcess.exitCode === null) ytProcess.kill();
            if (ffmpegProcess.exitCode === null) ffmpegProcess.kill();
            
            if (!bufferStream.destroyed) bufferStream.destroy();
        } catch (e) { console.error('Cleanup error:', e); }
        console.log('停止串流 ');
    };

    res.on('close', cleanup);
    
    ytProcess.on('error', (err) => {
        console.error('yt-dlp Process Error:', err);
        cleanup();
    });
}

// 這裡是本地音樂處理邏輯
app.get('/local-music', (req, res) => {
    const fileName = req.query.file;
    const seekTime = req.query.ss || '0';
	const rawTitle = path.parse(fileName).name; 
    const safeTitle = encodeURIComponent(rawTitle); 
    if (!fileName) return res.status(400).send('Missing file');

    const filePath = path.join("public", fileName);
    if (!fs.existsSync(filePath)) return res.status(404).send('File not found');

    console.log(`準備播放: ${fileName}, seek: ${seekTime}`);

    // 先用 ffprobe 取得精確的總秒數 
    exec(`"${FFPROBE_PATH}" -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${filePath}"`, (error, stdout, stderr) => {
        
       
        let totalDuration = 0;
        if (!error && stdout) {
            totalDuration = Math.floor(parseFloat(stdout));
            console.log(`檔案總長度: ${totalDuration} 秒`);
        }

        // 設定 Header，利用 'icy-name' 傳遞秒數設定直接控制
        res.writeHead(200, {
            'Content-Type': 'audio/mpeg',
            'Transfer-Encoding': 'chunked',
            'Connection': 'close',
            'Cache-Control': 'no-cache',
            'icy-name': `TotalSec:${totalDuration};StartSec:${seekTime};Title:${safeTitle}` 
        });
		 
        // 啟動 FFmpeg 串流 (使用穩定參數)
        const ffmpeg = spawn(FFMPEG_PATH, [
            '-i', filePath,
            '-ss', seekTime,
            '-vn',
            '-acodec', 'libmp3lame',
            '-b:a', '128k',       
            '-ar', '44100',
            '-ac', '2',
            
            // 淨化參數 
            '-map_metadata', '-1',
            '-write_xing', '0',
            '-id3v2_version', '0',
            '-write_id3v1', '0',
            '-write_apetag', '0',

            '-f', 'mp3',
            '-'
        ], { stdio: ['ignore', 'pipe', 'pipe'] });

        ffmpeg.stdout.pipe(res);

        ffmpeg.on('error', (err) => {
            console.error('FFmpeg Error:', err);
            if (!res.writableEnded) res.end();
        });

        req.on('close', () => {
            if (ffmpeg.exitCode === null) ffmpeg.kill();
        });
    });
});

// 啟動
app.listen(PORT, '0.0.0.0', () => {
    console.log(`Server running on http://0.0.0.0:${PORT}`);
});