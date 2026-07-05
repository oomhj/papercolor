# LED 灯使用说明

PaperColor 有 2 颗 RGB LED（G21，SK6812/WS2812 串行协议），共用一根数据线，由 M5PM1 LDO3V3_EN_PP 供电。

---

## 一、API 使用

项目不封装独立 LED 驱动库，直接使用 `M5.Led` API：

```cpp
#include <M5Unified.hpp>

// 设置亮度 (0-255)
M5.Led.setBrightness(60);

// 设置双灯同色并刷新
M5.Led.setAllColor(r, g, b);    // 0-255 per channel
M5.Led.display();

// 熄灭
M5.Led.setBrightness(0);
M5.Led.display();
```

---

## 二、实际使用模式

### 2.1 静态颜色指示（各模块通用）

```cpp
// 辅助函数，各 App 自包含
static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    M5.Led.setBrightness(60);
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}
static void led_off() {
    M5.Led.setBrightness(0);
    M5.Led.display();
}

// 使用
led_set(0, 0, 255);   // 蓝色 = 连接中
led_set(0, 255, 0);   // 绿色 = 成功
led_set(255, 0, 0);   // 红色 = 失败
led_off();
```

### 2.2 呼吸灯效果（wifi_manager）

在 WiFi 重试时使用平滑呼吸灯，亮度递减指示重试次数：

```cpp
// led_breathe() - 2s 周期呼吸，支持超时和停止标志
led_breathe(r, g, b, min_brightness, max_brightness, duration_ms, &stop_flag);
```

| 重试次数 | 亮度范围 | 含义 |
|----------|---------|------|
| 第 1 次 | 10–120 | 稍有信心 |
| 第 2 次 | 5–80 | 信心降低 |
| 第 3 次 | 3–50 | 即将放弃 |

---

## 三、颜色约定

| 状态 | 颜色 | 效果 | 使用场景 |
|------|------|------|---------|
| 连接中 | 蓝色 | 慢速呼吸（2s 周期） | WiFi 连接、HTTP 下载，亮度递减表示重试次数 |
| 连接成功 | 绿色 | 常亮 3 秒后熄灭 | WiFi 连接成功、数据获取成功 |
| 连接失败 | 红色 | 常亮 | WiFi 失败、HTTP 错误 |
| 断线 | 橙色 | 常亮 | 运行时 WiFi 断开 |
| AP 配网中 | 蓝色 | 快速呼吸（500ms 周期） | AP 待机，等待客户端连接 |
| AP 配置中 | 青色 | 常亮 | 客户端正在操作配网页 |
| 按键反馈 | 蓝→绿 | 常亮过渡 | 长按 BTN 3 秒到达时变色 |
| 熄灭 | 黑 | — | 空闲、休眠 |

---

## 四、代码分布

| 文件 | 使用方式 |
|------|---------|
| `wifi/wifi_manager.cpp` | `led_breathe()` 呼吸灯 + `wifi_mgr_update_led()` 状态灯 |
| `apps/news/news_app.cpp` | `led_set()` / `led_off()` 静态指示 |
| `apps/album/album_app.cpp` | `M5.Led.*` 直接调用 |

---

## 五、注意事项

- `setBrightness()` 是全局值，调用 `display()` 时生效
- `setAllColor()` 设置双灯同色但不刷新，须调用 `display()`
- 每次调用 `display()` 产生约 350µs 的 WS2812 时序信号
- 深度休眠前务必调用 `setBrightness(0)` + `display()` 熄灭 LED
