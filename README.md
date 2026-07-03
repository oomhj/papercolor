# M5Stack PaperColor 项目模板

基于 ESP-IDF v5.5 的 [M5Stack PaperColor](https://docs.m5stack.com/en/core/PaperColor) 开发板项目模板。

## 硬件规格

| 规格 | 参数 |
|---|---|
| **SoC** | ESP32-S3R8，双核 Xtensa LX7 @ 240MHz |
| **Flash / PSRAM** | 16MB / 8MB (Octal) |
| **屏幕** | 4" E-Paper E6 Full-Color ED2208-DOA (EL040EF1), 400×600 |
| **音频** | ES8311 编解码 + ES7210 ADC (AEC) + AW8737A 功放 |
| **传感器** | SHT40 温湿度, RX8130CE RTC |
| **存储** | microSD (SPI) |
| **电源** | M5PM1 PMU + 1250mAh 锂电池 |
| **待机/满载** | 92.53 μA / 211.97 mA |

## 快速开始

### 方式一：ESP-IDF（推荐）

```bash
# 确保子模块已拉取
git submodule update --init components/M5GFX components/M5Unified

# 编译
idf.py set-target esp32s3
idf.py build

# 烧录
idf.py -p /dev/tty.usbmodem1101 flash

# 监视串口
idf.py -p /dev/tty.usbmodem1101 monitor
```

### 方式二：PlatformIO（备用）

```bash
pio run -e m5stack-papercolor-arduino
pio run -e m5stack-papercolor-idf
```

## 项目结构

```
PaperColor/
├── CMakeLists.txt             # 顶层 CMake (ESP-IDF)
├── partitions.csv             # 分区表
├── sdkconfig.defaults         # IDF 默认配置
├── platformio.ini             # PlatformIO 配置
│
├── main/                      # 主组件
│   ├── CMakeLists.txt         # 组件 CMake
│   ├── main.cpp               # 入口
│   ├── idf_component.yml      # 组件管理器依赖
│   ├── hal/
│   │   ├── hal.h              # 硬件抽象层 API
│   │   └── hal.cpp            # 硬件抽象层实现
│   └── apps/
│       └── template/           # 应用模板（参考用）
│           ├── template_app.h
│           └── template_app.cpp
│
├── include/
│   └── config.h               # 管脚映射 & 硬件常量
│
├── components/                # Git 子模块
│   ├── M5GFX/
│   └── M5Unified/
│
└── m5_demo/                   # 官方 Demo（独立项目，参考用）
```

## 初始化顺序

参考 demo 项目的 init 顺序：

1. I2C 总线恢复（非冷启动时发送 9 个时钟脉冲）
2. `M5.begin()` — 初始化 I2C、按键、显示驱动
3. 创建 PSRAM 离屏画布 (`M5Canvas`)
4. `M5PM1.begin()` — 电源管理初始化
5. 使能 EPD 电源轨 (PYG0/PY_EPD_EN)
6. 使能音频电源轨 (G45)，默认关闭扬声器功放 (G46)
7. 检查电池电压，低于 3.1V 则关机

## 管脚映射速查

```
EPD:   CLK=G15, MOSI=G13, CS=G44, DC=G43, BUSY=G11, RST=G12
BTN:   A=G10, B=G9, C=G1, PWR=G0
Audio: MCLK=G42, LRCK=G41, BCLK=G40, DIN=G39, DOUT=G38
       PWR_EN=G45, SPK_EN=G46
SD:    CS=G47 (CLK+MOSI 与 EPD 共用)
I2C:   SCL=G2, SDA=G3 (所有外设共用)
RGB:   G21  |  IR:  G48  |  RTC_IRQ:  G7  |  Grove: G4/G5
```

## M5PM1 GPIO 电源控制

| PMU Pin | 功能 | 控制对象 |
|---------|------|----------|
| PYG0 | PY_EPD_EN | 墨水屏供电 |
| PYG1 | CARD_DEC | SD 卡插入检测 |
| PYG2 | RTC_IRQ | RTC 唤醒中断 |
| PYG3 | PY_SD_PWR_EN | microSD 模块供电 |
| PYG4 | PY_SD_DET_EN | SD 检测上拉使能 |

## 应用开发

创建新应用参考 `main/apps/template/` 的生命周期模式：

```cpp
class MyApp {
public:
    bool init();        // 分配资源
    void deinit();      // 释放资源
    void start();       // 开始（渲染首屏）
    void stop();        // 停止
    void update();      // 主循环中周期性调用
};
```

更多参考：`m5_demo/main/apps/calendar/`（完整日历应用）。

## 参考

- [官方文档](https://docs.m5stack.com/en/core/PaperColor)
- [官方用户 Demo](https://github.com/m5stack/M5PaperColor-UserDemo)
- `m5_demo/` — 官方 Demo 完整项目（含 Wi-Fi、Web 服务器、Ezdata 云推送）
