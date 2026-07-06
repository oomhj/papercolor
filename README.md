# PaperColor — E-Ink Photo Slideshow

![Build](https://img.shields.io/badge/build-passing-brightgreen)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue)
![License](https://img.shields.io/badge/license-MIT-green)

M5Stack PaperColor 上的自动相册固件。每天联网下载 Bing 每日壁纸，在 4" 全彩 E-Ink 屏上每 30 分钟自动轮播。**深度睡眠续航 1-2 个月。**

## 功能

| 功能 | 说明 |
|------|------|
| 📸 **自动下载** | 每天连网下载 10 张 Bing 壁纸到 SD 卡 |
| 🔋 **低功耗** | ESP32 Deep Sleep，30 分钟定时唤醒翻页 |
| 🖼️ **幻灯片** | 10 张图片轮播，30 分钟/张，UP/DOWN 手动切换 |
| 💳 **无 SD 模式** | 无 SD 卡时单张显示，30 分钟自动刷新 |
| 🌐 **WiFi 配网** | UP+DOWN 同时按住 → AP 配网页，或写 `/sd/wifi.txt` |
| 🔋 **电量指示** | 五格电池图标位于 EPD 右上角 |
| 🔌 **断点续传** | 每下载一张即保存进度，断电重启后续传 |

## 快速开始

### 硬件要求

- M5Stack PaperColor 开发板
- microSD 卡（可选，无 SD 卡也能运行）
- USB-C 数据线

### 构建

```bash
# 方式一：Docker
docker run -d --rm --name papercolor-build \
  -v .:/workspaces/PaperColor -w /workspaces/PaperColor \
  espressif/idf:release-v5.5 sleep infinity

docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build"

# 方式二：本地 ESP-IDF
source /opt/esp/idf/export.sh
idf.py set-target esp32s3
idf.py build
```

### 烧录

```bash
# 列出串口
ls /dev/tty.usbmodem*

# 烧录（替换 PORT 为实际串口）
esptool.py --chip esp32s3 -p /dev/tty.usbmodem1101 -b 115200 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/papercolor_template.bin
```

### 首次使用

1. 烧录后设备自动启动，有 SD 卡则下载 10 张图片后开始轮播
2. 无 SD 卡则下载单张，每 30 分钟自动刷新
3. **UP+DOWN 同按 2 秒** → 进入配网模式
4. 连接 WiFi `PaperColor-XXXX` → 浏览器打开 `192.168.4.1` → 输入 WiFi 密码
5. 配网成功后自动回到相册

### SD 卡 WiFi 预配置

在 SD 卡根目录创建 `/sd/wifi.txt`：

```
MyWiFi
MyPassword
114.114.114.114    ← DNS（可选）
```

## 按键操作

| 操作 | 功能 |
|------|------|
| **UP** | 上一张 |
| **DOWN** | 下一张 |
| **TOP 长按 2s** | 重新下载 10 张 |
| **UP + DOWN 同按** | WiFi 配网 |

## 低功耗模式

```
开机 → 显示图片 → 60s 无操作 → deep sleep
  ├─ 30 分钟后 RTC 定时器唤醒 → 翻到下一张 → sleep
  ├─ 按键唤醒 → 正常模式 → 60s 空闲 → sleep
  └─ 电量 < 10% → 永久关机
```

每天下载新图：唤醒 → 连 WiFi → 下载 → 显示 → sleep

## LED 指示灯

| 灯 | 含义 |
|----|------|
| 🔵 蓝呼吸 | WiFi 连接中 / 加载中 |
| 🟢 常亮 | WiFi 已连接 |
| 🟢 闪 2s | 操作成功 |
| 🔴 闪 2s | 失败（WiFi/HTTP/解码） |
| 🟠 闪 2s | 没网络 |
| 🟡 闪 | AP 配网模式 |
| 灭 | 空闲 / 深度睡眠 |

## 项目结构

```
main/
├── main.cpp                  # 入口
├── hal/
│   ├── hal.h/cpp             # 硬件抽象层
│   ├── spi_bus.h/cpp         # SPI 总线仲裁（EPD + SD 共享）
│   ├── sd_card.h/cpp         # SD 卡挂载/读写
│   ├── battery.h/cpp         # 电池监控
│   └── led_driver.h/cpp      # RGB LED 驱动
├── apps/
│   └── album/
│       ├── album_app.h/cpp   # 相册应用
│       └── filter.h/cpp      # 抖动滤镜
└── wifi/
    ├── wifi_manager.h/cpp    # WiFi STA + AP
    └── wifi_provisioning.h/cpp  # 配网服务器
```

## 配置文件

`/sd/album/config.txt`：

```
ssid=MyNetwork
pass=MyPassword
dns=114.114.114.114
updated=20260706
```

## 技术栈

- **SoC**: ESP32-S3R8 (240MHz, 16MB Flash + 8MB PSRAM)
- **Display**: 4" ED2208 E-Ink, 400×600, 8-bit color
- **WiFi**: ESP-IDF `esp_wifi` + `esp_http_client`
- **JPEG Decode**: `esp_new_jpeg` → RGB565 → Floyd-Steinberg dither
- **Sleep**: ESP32 Deep Sleep (RTC timer + GPIO wake), ~100µA
- **RTC**: RX8130CE (battery-backed RAM for slide index persistence)
- **Power**: M5PM1 PMU, 1250mAh Li-Po

## 环境

- ESP-IDF v5.5
- 组件：M5GFX, M5Unified, M5PM1, esp_new_jpeg, esp_http_client

## 开源协议

MIT License — 详见 [LICENSE](LICENSE)。
