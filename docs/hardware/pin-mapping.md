# 管脚映射

PaperColor 所有 GPIO 的完整功能定义。

---

## 系统 I2C 总线

所有 I2C 设备共享一条总线：

| 信号 | GPIO | 电平 |
|------|------|------|
| SCL | G2 | 3.3V |
| SDA | G3 | 3.3V |

| 地址 | 设备 | 功能 |
|------|------|------|
| 0x18 | ES8311 | 音频编解码器 |
| 0x40 | ES7210 | 音频 ADC（麦克风） |
| 0x6e | M5PM1 | 电源管理单元 |
| 0x32 | RX8130CE | 实时时钟 |
| 0x44 | SHT40 | 温湿度传感器 |

---

## E-Paper 显示屏

驱动 IC: EL040EF1 (ED2208-DOA) — 4" E6 Full-Color

| 信号 | GPIO | 说明 |
|------|------|------|
| EPD_SPI_CLK | G15 | SPI 时钟（与 microSD 共用） |
| EPD_SPI_MOSI | G13 | SPI 数据（与 microSD 共用） |
| EPD_CS | G44 | 片选 |
| EPD_DC | G43 | 数据/命令选择 |
| EPD_BUSY | G11 | 忙标志 |
| EPD_RST | G12 | 复位 |

| 参数 | 值 |
|------|-----|
| 分辨率 | 400 × 600 |
| 色深 | 8-bit |
| 供电 | M5PM1 PYG0 (PY_EPD_EN) |

---

## 按键

所有按键为低电平有效，内部上拉。

| 按键名 | GPIO | M5Unified 对象 | 功能约定 |
|--------|------|----------------|----------|
| BTN-TOP | G1 | `M5.BtnC` | 顶部按键（长按 5s 休眠） |
| BTN-UP | G9 | `M5.BtnB` | 上/前一页（长按 3s 配网） |
| BTN-DOWN | G10 | `M5.BtnA` | 下/后一页 |
| BTN-PWR | G0 | — | 电源键 (ON/OFF/RESET/BOOT) |

---

## RGB LED

| 信号 | GPIO | 说明 |
|------|------|------|
| RGB_DATA | G21 | SK6812/WS2812 单线控制 |
| 数量 | 2 | 串联 |
| 供电 | M5PM1 LDO3V3_EN_PP (PY_RGB_PWR_EN) |

---

## IR 发射

| 信号 | GPIO |
|------|------|
| IR_TX | G48 |

---

## 音频系统

| 信号 | GPIO | 设备 | 说明 |
|------|------|------|------|
| I2S_MCLK | G42 | ES8311 / ES7210 | 主时钟 |
| I2S_LRCK | G41 | ES8311 / ES7210 | 帧时钟 |
| I2S_BCLK | G40 | ES8311 / ES7210 | 位时钟 |
| I2S_DIN | G39 | ES8311 | 音频数据入 (codec ← I2S) |
| I2S_DOUT | G38 | ES7210 | 音频数据出 (mic → I2S) |
| AUDIO_PWR_EN | G45 | ES8311 + ES7210 | 供电使能 |
| SPK_EN | G46 | AW8737A | 扬声器功放使能 |

| 参数 | 值 |
|------|-----|
| 编解码器 | ES8311 (0x18) |
| 麦克风 ADC | ES7210 (0x40)，集成 AEC 回声消除 |
| 功放 | AW8737A，驱动 1W @ 8Ω 喇叭 |

---

## microSD 卡

| 信号 | GPIO |
|------|------|
| SD_CS | G47 |
| SD_CLK | G15 (与 EPD 共用 SPI 时钟) |
| SD_MOSI | G13 (与 EPD 共用 SPI 数据) |
| SD_MISO | G14 |

供电由 M5PM1 PYG3 (PY_SD_PWR_EN) 控制。
插入检测由 M5PM1 PYG4 (PY_SD_DET_EN) + PYG1 (CARD_DEC) 实现。

---

## RTC — RX8130CE

| 信号 | GPIO |
|------|------|
| RTC_IRQ | G7 |
| SCL | G2 |
| SDA | G3 |

地址: 0x32

---

## SHT40 温湿度传感器

| 信号 | GPIO |
|------|------|
| SCL | G2 |
| SDA | G3 |

地址: 0x44

---

## HY2.0-4P 扩展口 (PORT.A)

| 引脚 | 颜色 | 信号 |
|------|------|------|
| 1 | 黑 | GND |
| 2 | 红 | 5V (由 M5PM1 BOOST5V_EN_PP 控制) |
| 3 | 黄 | G4 |
| 4 | 白 | G5 |

---

## M5PM1 GPIO 功能

| PMU Pin | 信号名 | 功能 |
|---------|--------|------|
| PYG0 | PY_EPD_EN | 墨水屏供电开关 |
| PYG1 | CARD_DEC | microSD 卡插入检测 |
| PYG2 | RTC_IRQ | RTC 唤醒中断输入 |
| PYG3 | PY_SD_PWR_EN | microSD 模块供电开关 |
| PYG4 | PY_SD_DET_EN | SD 检测上拉使能 |

## 电源域

| 控制信号 | 控制对象 | 来源 |
|----------|----------|------|
| DCDC3V3_EN_PP | 3V3_L2 电源层 | M5PM1 |
| LDO3V3_EN_PP | RGB LED 供电 | M5PM1 |
| BOOST5V_EN_PP | Grove 扩展口 5V 输出 | M5PM1 |
| PY_EPD_EN (PYG0) | 墨水屏 | M5PM1 |
| PY_SD_PWR_EN (PYG3) | microSD 模块 | M5PM1 |
| AUDIO_PWR_EN (G45) | 音频编解码 + 麦克风 | GPIO 直控 |
| SPK_EN (G46) | 扬声器功放 | GPIO 直控 |

## Strapping 引脚（避免作为普通 IO 使用）

以下引脚在启动时有特殊功能，不要作为普通 GPIO 使用：
G0, G1, G2, G3, G8, G9, G18, G43, G46
