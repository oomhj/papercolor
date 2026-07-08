# Album 相册 — 设计与实现

PaperColor 上的每日照片幻灯片。自动下载 Bing 壁纸，在 E-Ink 屏上 30 分钟轮播。

---

## 一、架构概览

```
Bing API ─→ HTTP ─→ JPEG ↓
                         esp_jpeg_dec decode ─→ RGB565 ─→ Floyd-Steinberg ─→ EPD
SD card ─→ JPG 缓存 ──────↑
```

### 模块架构

```
AlbumApp (编排器)
  ├─ SlideShow        — 轮播 + 下载调度 + 日期跟踪
  │   ├─ ImageDownloader — HTTP fetch → SD atomic save
  │   └─ ImageRenderer   — JPEG decode → dither → EPD refresh
  ├─ PowerManager     — 空闲计时 → deep sleep → RTC RAM
  └─ WiFi             — wifi_manager + wifi_provisioning
```

---

## 二、双模式

| 模式 | 触发条件 | 行为 |
|------|---------|------|
| **SD 模式** | SD 卡已插入 | 10 张幻灯片，每日更新，断点续传 |
| **无 SD 模式** | 无 SD 卡 | 下载 1 张显示，30 分钟自动刷新 |

---

## 三、数据流

### SD 模式启动流程

```
启动:
  挂载 SD → 扫描 /sd/album/ 已有图片 → 直接显示第一张（<1s）
  → 检查 config.txt updated= 日期
    → 新的一天 → 后台下载 10 张
    → 同一天且图片不足 → 后台续传
    → 已完成 → 等待 30 分钟定时翻页

翻页（自动/手动）:
  RTC 定时唤醒（30min）→ 读取 RTC RAM 索引 → 翻到下一张 → deep sleep
  按键 UP/DOWN → 翻到上一张/下一张 → 60s 空闲 → deep sleep

刷新（TOP 长按）:
  下载第 1 张 → 显示 → 后台续传 2..10
  → 下载失败时从 SD 恢复已有图片
```

### 下载管线

```
HTTP: GET → 302 redirect → 200 OK (JPEG)
  ↓
找到 FFD8 标记 → 跳过 HTML 前缀
  ↓
SD 原子写入: .tmp → rename() → .jpg
  ↓
写 config.txt updated=YYYYMMDD (断点续传)
  ↓
失败: backoff (5/10/20/40/60 min) → 5 次后放弃
```

---

## 四、按键映射

| 操作 | 功能 |
|------|------|
| **UP** (G9) | 上一张 |
| **DOWN** (G10) | 下一张 |
| **TOP 长按** (G1) | 重新下载 10 张 |
| **UP + DOWN 同按** | WiFi 配网（SD → AP） |

---

## 五、技术实现

| 模块 | 方案 |
|------|------|
| **JPEG 解码** | `esp_jpeg_dec` → RGB565_BE，比例缩放至 400px 高度 |
| **抖动** | Floyd-Steinberg 误差扩散，适配 EPD 8-bit 色域 |
| **EPD 刷新** | `epd_quality`（~1.5s），`epd_fastest`（~500ms） |
| **HTTP** | `esp_http_client`，20s 超时，5 次重定向 |
| **WiFi** | `wifi_manager`（NVS 3 槽位 + SD wifi.txt + AP 配网） |
| **SD 卡** | `sd_card` 模块（FatFS，SPI 仲裁） |
| **低功耗** | ESP32 Deep Sleep，30min RTC 定时器 + 按键 ext1 唤醒 |
| **RTC** | RX8130CE，RAM 持久化幻灯片索引 |
| **配置** | `config_file`（key=value，atomic tmp+rename） |

---

## 六、文件结构

```
main/apps/album/
├── album_app.h/cpp            # 应用生命周期 + 编排
├── slide_show.h/cpp           # 轮播逻辑 + 下载调度 + 日期
├── image_downloader.h/cpp     # HTTP 下载 + SD 保存
├── image_renderer.h/cpp       # JPEG 解码 + dither + EPD
├── power_manager.h/cpp        # 空闲计时 + deep sleep + RTC RAM
└── filter.h/cpp               # Floyd-Steinberg / JJN 抖动
```

依赖：
- `hal/hal` — 硬件抽象（I2C / PMU / EPD / 传感器 / RTC）
- `hal/spi_bus` — SPI2_HOST 仲裁（EPD vs SD）
- `hal/sd_card` — SD 卡挂载 + FatFS
- `hal/battery` — 电池监控（30s 缓存）
- `hal/led_driver` — 异步 LED 驱动
- `hal/config_file` — key=value 文件读写
- `hal/button` — 按钮别名定义
- `wifi/wifi_manager` — STA/AP + NVS 存储
- `wifi/wifi_provisioning` — Captive portal 配网

---

## 七、SD 卡文件结构

```
/sd/album/
  ├── 1.jpg ~ 10.jpg    ← 缓存图片（原子写入 .tmp → rename）
  └── config.txt         ← 配置（ssid, pass, dns, updated）
```

---

## 八、低功耗模式

```
运行 → 60s 空闲 → deep sleep
  ├─ 30min RTC 唤醒 → idx+1 翻页 → sleep
  ├─ 按键唤醒 → 保持当前 idx → 60s 空闲 → sleep
  └─ 电量 < 10% → 永久关机（无 timer，仅按钮唤醒）

RTC RAM 持久化（4 字节，RX8130 电池后备）:
  0x20: current_idx  (1-10)
  0x21: date low byte
  0x22: date high byte
```

---

## 九、实施状态

| 功能 | 状态 |
|------|------|
| 单图显示 | ✅ |
| 10 张幻灯片 | ✅ |
| SD 卡缓存 | ✅ |
| 无 SD 模式 | ✅ |
| Deep sleep | ✅ |
| 电量指示 | ✅ |
| WiFi 配网 | ✅ |
| 断点续传 | ✅ |
| 原子文件写入 | ✅ |
| 指数退避重试 | ✅ |
| 802.1x 企业认证 | ✅ |
| Kconfig 可配 URL | 📅 |
| OTA 升级 | 📅 |
