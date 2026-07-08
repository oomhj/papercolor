# 相册功能

PaperColor 上的每日照片幻灯片。自动下载 Bing 壁纸，SD 卡缓存 10 张，30 分钟自动轮播。

---

## 一、功能概述

| 项目 | 内容 |
|------|------|
| 应用目录 | `main/apps/album/` |
| 数据源 | `https://bing.img.run/rand_1366x768.php` → 302 重定向 → Bing CDN |
| 交互方式 | 3 个物理按键 + 自动定时 |
| 存储 | SD 卡（可选）/ 无 SD 单图模式 |
| 低功耗 | ESP32 Deep Sleep（~100µA，30 分钟定时唤醒） |

---

## 二、文件结构

```
main/apps/album/
├── album_app.h/cpp        # 应用编排器：生命周期、按钮、WiFi 协调
├── slide_show.h/cpp       # 轮播控制器：导航、下载调度、日期跟踪
├── image_downloader.h/cpp # HTTP 下载：fetch + SD 原子保存
├── image_renderer.h/cpp   # 渲染管线：JPEG 解码 → dither → EPD
├── power_manager.h/cpp    # 低功耗：空闲计时、deep sleep、RTC RAM
└── filter.h/cpp           # 抖动滤镜：Floyd-Steinberg / JJN
```

### 模块职责

```
AlbumApp ── 编排器
  ├─ 管理 init/start/update/deinit 生命周期
  ├─ 处理按钮事件（UP/DOWN/TOP + 组合键）
  ├─ 协调 WiFi 配网（UP+DOWN 同按）
  └─ 委托 SlideShow / PowerManager

SlideShow ── 轮播核心
  ├─ 图片索引管理（1..10）
  ├─ 下载调度（断点续传 + 指数退避）
  ├─ 自动翻页（30min）和每日更新检测
  └─ SD 卡扫描 / 日期读写

PowerManager ── 低功耗
  ├─ 60s 空闲自动休眠
  ├─ deep sleep（30min timer + button ext1）
  └─ RTC RAM 持久化（idx + date）

ImageDownloader ── HTTP
  ├─ HTTP GET + 重定向跟随（5 次）
  ├─ JPEG 起始标记检测（FFD8）
  └─ SD 原子保存（.tmp → rename）

ImageRenderer ── 渲染
  ├─ JPEG 解码（esp_jpeg_dec, RGB565, 比例缩放）
  ├─ Floyd-Steinberg 抖动
  ├─ 电池图标 overlay
  └─ EPD 刷新（epd_quality / epd_fastest）
```

---

## 三、按键映射

| 操作 | 功能 |
|------|------|
| **UP** (G9) | 上一张 |
| **DOWN** (G10) | 下一张 |
| **TOP 长按** (G1) | 重新下载 10 张 |
| **UP + DOWN 同按** | WiFi 配网 |

---

## 四、数据流

### SD 模式（10 张轮播）

```
启动:
  挂载 SD → 扫描 /sd/album/ 已有图片 → 显示第一张（<1s）
  → 检查 config.txt updated= 日期
    → 新的一天 → dl 1.jpg→show→queue 2..10
    → 同一天且图片不足 → 后台续传
    → 已完成 → 等待 30 分钟定时翻页

翻页:
  自动 (30min): 当前 index + 1 (循环 1→2→...→10→1)
  手动 (UP/DOWN): 上一张 / 下一张，重置 30min 计时器

刷新 (TOP 长按):
  重置 → dl 1.jpg → 立即显示 → 后台 2..10.jpg

下载管线:
  HTTP GET → 302 redirect → 200 JPEG → 找 FFD8 标记
  → SD 原子写入 (.tmp → rename)
  → 每张写 config.txt updated= (断点续传)
  → 失败: backoff 5/10/20/40/60min, 5 次后放弃
```

### 无 SD 模式（单图）

```
启动: dl 1 张 → 显示 → 30min 自动刷新
TOP 长按: 手动刷新
```

---

## 五、配置

### SD 文件结构

```
/sd/album/
  ├── 1.jpg ~ 10.jpg    ← 缓存图片
  └── config.txt         ← 配置（key=value）
```

### config.txt 格式

```
ssid=MyNetwork
pass=MyPassword
dns=114.114.114.114
auth=psk                    # 或 enterprise
identity=...                # enterprise 模式
username=...                # enterprise 模式
updated=20260708             # YYYYMMDD，每次下载后更新
```

### 日期优先级

```
config.txt updated=  （主）
    ↓ 不存在或无效
RTC RAM date          （后备）
    ↓ 不存在或无效
M5.Rtc.getDateTime()  （实时）
```

---

## 六、技术实现

| 模块 | 方案 |
|------|------|
| **JPEG 解码** | `esp_jpeg_dec` → RGB565_BE，400px 高度等比缩放 |
| **抖动** | Floyd-Steinberg 误差扩散（/16 kernel） |
| **EPD 刷新** | `epd_quality`（~1.5s），`epd_fastest`（~500ms） |
| **HTTP** | `esp_http_client`，20s 超时，5 次重定向 |
| **WiFi** | `wifi_manager`（NVS 3 槽位 + AP 配网） |
| **SD 卡** | `sd_card`（FatFS, SPI 仲裁） |
| **低功耗** | ESP32 Deep Sleep, 30min timer + button ext1 |
| **RTC** | RX8130CE, RAM 持久化（idx + date, 4 字节） |
| **配置** | `config_file`（key=value, atomic tmp+rename） |
