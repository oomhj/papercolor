# Album 相册 — 设计与实现

PaperColor 上的每日照片幻灯片。自动下载 Bing 壁纸，在 E-Ink 屏上 30 分钟轮播。

---

## 一、架构概览

```
Bing API ─→ HTTP ─→ JPEG ↓
                         esp_new_jpeg decode ─→ RGB565 ─→ Floyd-Steinberg ─→ EPD
SD card ─→ JPG 缓存 ──────↑
```

## 二、双模式

| 模式 | 触发条件 | 行为 |
|------|---------|------|
| **SD 模式** | SD 卡已插入 | 10 张幻灯片，每日更新，断点续传 |
| **无 SD 模式** | 无 SD 卡 | 下载 1 张显示，30 分钟自动刷新 |

## 三、数据流

```
启动：
  挂载 SD → 扫描 /sd/album/ 已有图片 → 直接显示第一张（<1s）
  → 检查 config.txt updated= 日期
    → 新的一天 → 后台下载 10 张
    → 同一天且图片不足 → 后台续传
    → 已完成 → 等待 30 分钟定时翻页

翻页（自动/手动）：
  RTC 定时唤醒（30min）→ 读取 RTC RAM 索引 → 翻到下一张 → deep sleep
  按键 UP/DOWN → 翻到上一张/下一张 → 60s 空闲 → deep sleep

刷新（TOP 长按）：
  删除旧图 → 下载第 1 张 → 显示 → 后台续传 2..10
  → 下载失败时从 SD 恢复已有图片
```

## 四、按键映射

| 操作 | 功能 |
|------|------|
| **UP** (G9) | 上一张（fast EPD 模式） |
| **DOWN** (G10) | 下一张 |
| **TOP 长按** (G1) | 重新下载 10 张 |
| **UP + DOWN 同按** | WiFi 配网（SD → AP） |

## 五、技术实现

| 模块 | 方案 |
|------|------|
| **JPEG 解码** | `esp_new_jpeg` → RGB565_BE，比例缩放至 400px 高度 |
| **抖动** | Floyd-Steinberg 误差扩散，适配 EPD 8-bit 色域 |
| **EPD 刷新** | `epd_quality`（~1.5s），`epd_fastest` 已实现但未启用 |
| **HTTP** | `esp_http_client`，20s 超时，5 次重定向 |
| **WiFi** | `wifi_manager`（NVS + SD wifi.txt + AP 配网） |
| **SD 卡** | `sd_card` 模块（FatFS，SPI 仲裁） |
| **低功耗** | ESP32 Deep Sleep，30min RTC 定时器 + 按键 ext1 唤醒 |
| **RTC** | RX8130CE，RAM 持久化幻灯片索引 |
| **配置** | `/sd/album/config.txt`（key=value） |

## 六、文件结构

```
main/apps/album/
├── album_app.h         # 应用生命周期 + 状态
├── album_app.cpp       # 主逻辑
├── filter.h            # 抖动滤镜声明
└── filter.cpp          # Floyd-Steinberg 实现
```

依赖：
- `hal/hal` — 硬件抽象
- `hal/battery` — 电池监视
- `hal/sd_card` — SD 卡
- `hal/spi_bus` — SPI 仲裁
- `wifi/wifi_manager` — WiFi 管理

## 七、SD 卡文件结构

```
/sd/album/
  ├── 1.jpg ~ 10.jpg    ← 缓存图片
  └── config.txt         ← 配置（ssid, pass, dns, updated）

/sd/wifi.txt             ← WiFi 预配置（兼容旧版）
```

## 八、低功耗模式

```
运行 → 60s 空闲 → deep sleep
  ├─ 30min RTC 唤醒 → 翻页 → sleep
  ├─ 按键唤醒 → 正常模式 → 60s 空闲 → sleep
  └─ 电量 < 10% → 永久关机（需充电）
```

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
| Kconfig 可配 URL | 📅 |
| OTA 升级 | 📅 |
| 电量显示到 EPD | ✅ 五格电池图标 |
