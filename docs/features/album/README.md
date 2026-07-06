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

## 二、文件结构

```
main/apps/album/
├── album_app.h         # 应用生命周期 + 状态
├── album_app.cpp       # 主逻辑
└── filter.h/cpp        # Floyd-Steinberg 抖动

main/hal/
├── battery.h/cpp       # 电池监控
├── sd_card.h/cpp       # SD 卡挂载
└── spi_bus.h/cpp       # SPI 仲裁（EPD + SD）
```

## 三、按键映射

| 操作 | 功能 |
|------|------|
| **UP** (G9) | 上一张 |
| **DOWN** (G10) | 下一张 |
| **TOP 长按** (G1) | 重新下载 10 张 |
| **UP + DOWN 同按** | WiFi 配网 |

## 四、数据流

```
HTTP 下载:
  TOP 长按 → 删除旧图 → 下载 1.jpg → 显示
  → 后台续传 2..10.jpg → 每张存进度 → 完成 → 5s → deep sleep

SD 加载（翻页）:
  UP/DOWN → 读 SD 缓存 → esp_new_jpeg 解码
  → Floyd-Steinberg 抖动 → pushImage → EPD refresh

低功耗:
  RTC 定时唤醒 → 翻到下一张 → deep sleep
  按键唤醒 → 正常模式 → 60s 空闲 → deep sleep
```

## 五、配置

WiFi 配置：`/sd/wifi.txt`（SSID + 密码 + DNS，可选）

相册配置：`/sd/album/config.txt`（ssid, pass, dns, updated）
