const YTDlpWrap = require('yt-dlp-wrap').default;
const fs = require('fs');
const path = require('path');
// 引入靜態路徑 (這些路徑原本指向 node_modules 深處)
const ffmpegSrc = require('ffmpeg-static');
const ffprobeSrc = require('ffprobe-static').path;

// 定義目標資料夾與檔案
const toolsDir = path.join(__dirname, 'tools');
const ytDlpPath = path.join(toolsDir, 'yt-dlp.exe');
const ffmpegDest = path.join(toolsDir, 'ffmpeg.exe');
const ffprobeDest = path.join(toolsDir, 'ffprobe.exe');

//  確保 tools 資料夾存在
if (!fs.existsSync(toolsDir)){
    fs.mkdirSync(toolsDir);
    console.log('QB 建立 tools 資料夾...');
}

//  複製 FFmpeg 與 FFprobe
console.log('正在將 FFmpeg/FFprobe 複製到 tools 資料夾...');
try {
    fs.copyFileSync(ffmpegSrc, ffmpegDest);
    console.log('ffmpeg.exe 複製成功');
    
    fs.copyFileSync(ffprobeSrc, ffprobeDest);
    console.log('ffprobe.exe 複製成功');
} catch (err) {
    console.error('複製 FFmpeg 失敗:', err);
}

// 下載 yt-dlp
console.log('正在下載最新版 yt-dlp...');
YTDlpWrap.downloadFromGithub(ytDlpPath)
    .then(() => console.log('yt-dlp 下載完成！繼續等待其他套件安裝完成'))
    .catch(err => console.error('yt-dlp 下載失敗:', err));