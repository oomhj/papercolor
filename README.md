# M5Stack PaperColor

基于 PlatformIO 的 [M5Stack PaperColor](https://docs.m5stack.com/en/core/PaperColor) 开发板项目。

## 硬件规格

| 规格 | 参数 |
|---|---|
| **SoC** | ESP32-S3R8，双核 Xtensa LX7 @ 240MHz |
| **Flash / PSRAM** | 16MB / 8MB |
| **Wi-Fi** | 2.4 GHz |
| **屏幕** | 4" E-Paper (E6 Full-Color) ED2208-DOA (EL040EF1), 400×600 |
| **输入电源** | USB Type-C DC 5V |
| **电池** | 1250mAh 锂电池 |
| **音频编解码器** | ES8311 |
| **麦克风** | MEMS 麦克风 + ES7210 音频 ADC（集成 AEC 回声消除） |
| **扬声器** | 1W @ 8Ω 2520 扬声器 + AW8737A 音频功放 |
| **温湿度传感器** | SHT40 |
| **拓展存储** | microSD |
| **RTC** | RX8130CE |
| **用户按键** | 3× 用户按键 + 1× 电源按键 (ON/OFF/RESET/BOOT) |
| **待机功耗** | 92.53 μA |
| **满载功耗** | 211.97 mA |
| **产品尺寸** | 70.8 × 103.9 × 8.5 mm |
| **产品重量** | 73.3 g |

## 快速开始

```bash
# 安装依赖
pio lib install

# 编译
pio run

# 编译并上传
pio run --target upload

# 监视串口
pio device monitor
```

## 项目结构

```
.
├── platformio.ini      # PlatformIO 项目配置
├── src/                # 源代码
│   └── main.cpp        # 主程序入口
├── include/            # 头文件
│   └── config.h        # 全局配置
├── lib/                # 私有库
├── data/               # SPIFFS/LittleFS 数据文件
└── test/               # 单元测试
```

## 按键功能

| 按键 | GPIO | 功能 |
|---|---|---|
| BTN-A | 0 | 刷新屏幕 |
| BTN-B | 1 | 显示电源信息 |
| BTN-C | 2 | 进入深度休眠 |
