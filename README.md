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

### 方式一：ESP-IDF 容器编译 + 主机烧录（推荐）

> 详细步骤见 [`docs/build-guide.md`](docs/build-guide.md)

```bash
# 编译（Docker 容器内）
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build"

# 烧录（主机 esptool）
cd build
esptool.py --chip esp32s3 -p /dev/tty.usbmodem1101 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 papercolor_template.bin
```

```bash
# 初始化子模块（M5GFX、M5Unified）
git submodule update --init

# 编译
idf.py set-target esp32s3
idf.py build

# 烧录
idf.py -p /dev/tty.usbmodem1101 flash

# 监视串口（退出：Ctrl+]）
idf.py -p /dev/tty.usbmodem1101 monitor
```

### 已有应用切换

`main/main.cpp` 是入口文件，当前运行的是 **Album 网络相册**。
如需切换为 **News 新闻阅读器**，将 `main.cpp` 内容替换为：

```cpp
#include "hal/hal.h"
#include "apps/news/news_app.h"
#include <esp_log.h>

static const char* TAG = "PaperColor";
static NewsApp s_news;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PaperColor News Reader");
    pc_hal_init();
    s_news.init();
    s_news.start();

    while (1) {
        pc_hal_update();
        s_news.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

### 方式二：PlatformIO（备用）

```bash
cd m5_demo
pio run -e m5stack-papercolor-arduino
pio run -e m5stack-papercolor-idf
```

## 项目结构

```
PaperColor/
├── CMakeLists.txt             # 顶层 CMake (ESP-IDF)
├── partitions.csv             # 分区表
├── sdkconfig.defaults         # IDF 默认配置
│
├── main/                      # 主组件
│   ├── CMakeLists.txt         # 组件 CMake（含所有源文件注册）
│   ├── main.cpp               # 入口（当前运行 Album 应用）
│   ├── idf_component.yml      # 组件管理器依赖
│   │
│   ├── hal/
│   │   ├── hal.h              # 硬件抽象层 API（pc_hal_* 系列）
│   │   └── hal.cpp            # 硬件抽象层实现
│   │
│   ├── apps/
│   │   ├── template/           # 应用模板（生命周期参考）
│   │   │   ├── template_app.h
│   │   │   └── template_app.cpp
│   │   ├── news/               # 热点新闻阅读器（P0）
│   │   │   ├── news_app.h/cpp        # 应用生命周期 + 主逻辑
│   │   │   ├── news_fetcher.h/cpp    # HTTP 下载封装
│   │   │   └── news_parser.h/cpp     # RSS XML 解析
│   │   └── album/              # 网络相册（P0）
│   │       ├── album_app.h
│   │       └── album_app.cpp
│   │
│   └── wifi/                  # WiFi 管理模块
│       ├── wifi_manager.h/cpp        # STA + AP 统一管理
│       ├── wifi_provisioning.h/cpp   # 配网服务器（DNS 劫持 + HTTP 页面）
│       └── www/                      # 配网页静态资源
│
├── include/
│   └── config.h               # 管脚映射 & 硬件常量
│
├── components/                # Git 子模块
│   ├── M5GFX/
│   └── M5Unified/
│
├── managed_components/        # IDF 组件管理器自动拉取
│
├── docs/                      # 设计文档
│   ├── hardware/              # 硬件方案
│   ├── features/              # 功能设计
│   ├── album-design.md        # 网络相册方案
│   └── news-display-design.md # 热点新闻方案
│
└── m5_demo/                   # 官方 Demo（独立 ESP-IDF 项目，参考用）
```

## 初始化顺序

HAL 初始化 `pc_hal_init()` 按以下顺序进行（见 `main/hal/hal.cpp`）：

1. **I2C 总线恢复** — 非冷启动时发送 9 个时钟脉冲释放被锁设备
2. **M5.begin()** — 初始化 I2C、按键、M5GFX 显示驱动
3. **离屏 Canvas** — 在 PSRAM 创建 `M5Canvas`（8-bit, 400×600）
4. **M5PM1.begin()** — 电源管理初始化
5. **EPD 电源使能** — PYG0 (PY_EPD_EN) HIGH
6. **音频电源使能** — G45 HIGH，G46 LOW（功放默认关闭）
7. **电池检测** — 低于 3.1V 自动关机保护

## 管脚映射速查

| 功能 | 信号 | GPIO |
|------|------|------|
| **EPD 显示屏** | CLK / MOSI / CS / DC / BUSY / RST | G15 / G13 / G44 / G43 / G11 / G12 |
| **按键** | A / B / C / PWR | G10 / G9 / G1 / G0 |
| **音频** | MCLK / LRCK / BCLK / DIN / DOUT | G42 / G41 / G40 / G39 / G38 |
| | PWR_EN / SPK_EN | G45 / G46 |
| **microSD** | CS / MISO（CLK+MOSI 与 EPD 共用） | G47 / G14 |
| **I2C 总线** | SCL / SDA | G2 / G3 |
| **RGB LED** | DATA | G21 |
| **红外** | IR_TX | G48 |
| **RTC** | RTC_IRQ | G7 |
| **Grove** | 扩展口 (HY2.0-4P) | G4 / G5 |

## M5PM1 GPIO 电源控制

| PMU Pin | 信号名 | 控制对象 |
|---------|--------|----------|
| PYG0 | PY_EPD_EN | 墨水屏供电 |
| PYG1 | CARD_DEC | microSD 卡插入检测 |
| PYG2 | RTC_IRQ | RTC 唤醒中断输入 |
| PYG3 | PY_SD_PWR_EN | microSD 模块供电 |
| PYG4 | PY_SD_DET_EN | SD 检测上拉使能 |

## WiFi 模块

`main/wifi/` 提供完整的 WiFi 功能（STA 连接 + AP 配网），API 见 `wifi_manager.h`：

```c
// 初始化
void wifi_mgr_init(void);

// STA 连接
bool wifi_mgr_connect_sta(uint32_t timeout_ms);
void wifi_mgr_disconnect_sta(void);
void wifi_mgr_start_retry_loop(bool is_reconnect);  // 自动退避重试

// AP 配网
void wifi_mgr_start_ap(void);
void wifi_mgr_stop_ap(void);
void wifi_mgr_trigger_provisioning(void);           // 手动触发配网

// 配置存储（NVS，最多 3 组）
bool wifi_mgr_save_network(int slot, const char* ssid, const char* pass);
bool wifi_mgr_load_network(int slot, char* ssid, size_t len, char* pass, size_t plen);

// 状态查询
wifi_state_t wifi_mgr_get_state(void);
const char*  wifi_mgr_get_ip(void);
bool         wifi_mgr_handle_buttons(void);          // 在 main loop 中调用
```

详情见 `docs/features/network/`。当前 P0 应用（news/album）使用内置硬编码 WiFi 连接，尚未集成 `wifi_manager`。

## 应用开发

### 生命周期模式

所有应用遵循统一的生命周期（参考 `main/apps/template/`）：

```cpp
class MyApp {
public:
    bool init();        // 分配资源
    void deinit();      // 释放资源
    void start();       // 开始运行
    void stop();        // 停止
    void update();      // 主循环中周期性调用
    void refresh();     // 手动触发刷新
};
```

已实现的应用：

| 应用 | 目录 | 状态 | 入口示例 |
|------|------|------|----------|
| 网络相册 Album | `apps/album/` | P0 已完成 | `main.cpp` 当前运行此应用 |
| 热点新闻 News | `apps/news/` | P0 已完成 | 替换 `main.cpp` 引用即可 |
| 官方日历 Calendar | `m5_demo/main/apps/calendar/` | 独立项目 | 参考用 |

### 硬件抽象层

通过 `hal/hal.h` 提供的 `pc_hal_*` API 操作硬件，不直接操作 GPIO：

```c
// 初始化
void pc_hal_init(void);
void pc_hal_update(void);           // 主循环中调用

// 显示
M5Canvas* g_canvas;                 // 离屏画布（PSRAM）
void pc_hal_display(void);          // Canvas → EPD

// 电源
uint16_t pc_hal_read_battery_mv(void);
float pc_hal_battery_pct(void);
bool pc_hal_is_charging(void);
void pc_hal_set_epd_power(bool on);
void pc_hal_deep_sleep(void);

// 传感器
bool pc_hal_read_sht40(float* temp_c, float* humidity);

// 调试屏
void pc_hal_draw_splash(void);
void pc_hal_show_power_info(void);
```

### 应用入口模板

```cpp
#include "hal/hal.h"
#include "apps/myapp/myapp.h"

extern "C" void app_main(void) {
    pc_hal_init();
    MyApp app;
    app.init(); app.start();
    while (1) { pc_hal_update(); app.update(); vTaskDelay(20); }
}
```

## 参考

- [M5Stack PaperColor 官方文档](https://docs.m5stack.com/en/core/PaperColor)
- [官方用户 Demo](https://github.com/m5stack/M5PaperColor-UserDemo)
- `m5_demo/` — 官方 Demo 完整项目（含 Wi-Fi、Web 服务器、Ezdata 云推送）
- `docs/` — 项目设计文档目录（硬件、网络、应用方案）
