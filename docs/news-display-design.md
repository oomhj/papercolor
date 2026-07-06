# 热点图片新闻 — 设计方案与实现

在 PaperColor 墨水屏上轮播热点新闻，支持标题显示和按键翻页。

> ⚠️ 已废弃：News app 已从项目中删除。此文档保留供参考。

---

## 一、整体架构

```
┌──────────────┐     HTTP/TLS      ┌───────────────────┐
│  RSS Feed    │ ◄──────────────►  │  PaperColor       │
│  (云端)      │                   │  ┌─────────────┐  │
│              │                   │  │ news_app    │  │
│ • 少数派sspai│                   │  │  ├ fetcher  │  │
│ • 可扩展     │                   │  │  ├ parser   │  │
│              │                   │  │  └ render   │  │
└──────────────┘                   │  └─────────────┘  │
                                   └───────────────────┘
```

## 二、数据源

### 当前（P0）：RSS Feed（少数派 sspai）

```c
static const char* RSS_URLS[] = {
    "https://sspai.com/feed",
};
```

- RSS 2.0 XML 格式
- 无需 API Key，完全免费
- 自动获取标题 + 摘要 + 发布日期

### 可选源

| 数据源 | 格式 | 说明 |
|--------|------|------|
| 少数派 sspai | RSS | ✅ 当前使用 |
| Bing 每日一图 | JSON | 图片 URL + 标题（无"热点"） |
| NewsAPI | JSON | 需 API Key，100 req/day |
| GitHub Raw 自建 | JSON | 完全可控 |

---

## 三、EPD 显示布局

横屏 600×400，每页显示一条新闻：

```
┌──────────────────────────────────────────────────────────────────┐
│  Hot News                          2026-07-03          1/10      │  ← 标题栏 (h=36, 深色背景)
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│                                                                  │
│             神舟二十号飞船成功发射                                   │  ← 标题 (Font6, 大号)
│             搭载三名航天员进入太空                                  │
│                                                                  │
│                                                                  │
│             新华社  |  2小时前                                    │  ← 来源 (Font2, 灰色)
│                                                                  │
│             2026年7月3日，神舟二十号载人飞船                        │  ← 摘要 (Font2, 多行)
│             在酒泉卫星发射中心...                                   │
│                                                                  │
│                                                                  │
│                                                                  │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  [A] << Prev    [B] Refresh    [C] Next >>                       │  ← 底部栏 (h=16)
└──────────────────────────────────────────────────────────────────┘
```

### 设计 vs 实现对比

| 元素 | 设计方案 | 当前实现 (P0) |
|------|---------|---------------|
| 图片区域 | 280×180 居中配图 | ❌ 尚未实现（P1 引入） |
| 标题 | Font4 或 Font6 | ✅ Font6 大号显示 |
| 来源 + 时间 | Font0 | ✅ Font2 灰色显示 |
| 摘要 | Font2, 2 行 | ✅ Font2, 最多 5 行 |
| 底部栏 h=30 | 三个按键 + 自动刷新时间 | ✅ 简化底部栏 h=16 |

### 关键坐标

| 区域 | 位置 | 大小 |
|------|------|------|
| 标题栏 | y=0 | h=36 |
| 标题文字 | y=60 | Font6 |
| 来源 + 日期 | y=130 | Font2 |
| 摘要 | y=180 | Font2, 每行 ~50 字符, 最多 5 行 |
| 底部栏 | h-16 | Font0 |

---

## 四、按键交互

| 按键 | 功能 | 实现 |
|------|------|------|
| BTN-A (G10) | 上一条新闻 | `_current_index--` → `render()` |
| BTN-B (G9) | 手动刷新 | `_needs_refresh = true` |
| BTN-C (G1) | 下一条新闻 | `_current_index++` → `render()` |

---

## 五、实现细节

### 5.1 HTTP 获取（news_fetcher.cpp）

- 封装 `esp_http_client_perform()`，自动处理 302 重定向
- 支持 HTTPS（`esp_crt_bundle_attach`）
- 动态缓冲区（初始 32KB，自动扩展至 ~2MB）
- 15s 超时

```cpp
fetch_result_t news_fetch_url(const char* url, uint32_t timeout_ms);
// 返回: {data: char*, length: size_t, err: esp_err_t}
// 调用者须 free(data)
```

### 5.2 RSS 解析（news_parser.cpp）

简单的标签匹配解析器，适用于标准 RSS 2.0：

- 按 `<item>` / `</item>` 分割条目
- 提取 `<title>`、`<description>`、`<pubDate>`、`<link>`、`<source>`
- 自动去除 `<![CDATA[...]]>` 包裹
- 解码 HTML 实体（`&amp;`、`&lt;`、`&gt;` 等）
- 标题截断 120 字符，摘要截断 300 字符

### 5.3 渲染（news_app.cpp 内联）

使用 M5GFX 字体和 Canvas 直接绘制：

```cpp
M5.Display.setEpdMode(epd_fastest);  // 快速刷新
g_canvas->fillScreen(TFT_WHITE);

// 标题栏
g_canvas->fillRect(0, 0, w, 36, 0x2118);
g_canvas->setFont(&fonts::Font2);
g_canvas->drawString("Hot News  2026-07-03", 12, 10);

// 标题
g_canvas->setFont(&fonts::Font6);
g_canvas->drawString(item.title, 20, 60);

// 来源
g_canvas->setFont(&fonts::Font2);
g_canvas->drawString("来源  |  日期", 20, 130);

// 摘要（自动换行）
g_canvas->setFont(&fonts::Font2);
// 每 50 字符换行，最多 5 行

g_canvas->pushSprite(0, 0);
M5.Display.display();
```

### 5.4 自动刷新

- 定时器：每 30 分钟自动触发 `_needs_refresh`
- 刷新流程：LED 蓝 → WiFi 连接 → HTTP GET → 解析 → LED 绿 → 渲染

### 5.5 WiFi 实现（独立，未使用 wifi_manager）

当前使用 `news_app.cpp` 中独立的 `wifi_connect_sta()`：

```cpp
// 硬编码 WiFi
#define NEWS_SSID "Jason-home"
#define NEWS_PASS "admin1234"

// 直接连接（25s 超时，轮询 ap_info）
while (deadline) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return true;
}
```

### 5.6 缓存

当前无持久缓存，数据保存在内存 `std::vector<NewsItem>` 中（最多 50 条）。
离线降级显示最后获取的内容，但重启后丢失。

---

## 六、文件结构

```
main/apps/news/
├── news_app.h            # 应用生命周期 + NewsItem 定义
├── news_app.cpp          # 主逻辑（生命周期、渲染、按键、WiFi）
├── news_fetcher.h        # HTTP 获取接口
├── news_fetcher.cpp      # esp_http_client 实现
├── news_parser.h         # RSS 解析接口
└── news_parser.cpp       # 标签匹配 RSS 解析器

依赖:
- hal/hal（硬件抽象）
- M5GFX（Canvas, Fonts）
- ESP-IDF: esp_http_client, esp_wifi, nvs_flash, esp-tls, mbedtls, json
```

> 设计中规划的 `news_renderer.h/cpp` 尚未创建（P1 图片渲染时引入）。

---

## 七、集成步骤

### 当前已集成

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS
        "main.cpp"
        "hal/hal.cpp"
        "apps/news/news_fetcher.cpp"    # ← news 文件
    ...
)
```

启动方式：将 `main.cpp` 中的 `AlbumApp` 替换为 `NewsApp`。

### 规划集成

| 组件 | 状态 | 说明 |
|------|------|------|
| hal 模式 ID | 📅 | 如需与 app_manager 配合 |
| wifi_manager | 📅 | 替代独立 wifi_connect |
| 图片渲染 | 📅 | P1 引入配图 |

---

## 八、实施路线

| 阶段 | 内容 | 状态 |
|------|------|------|
| **P0** | RSS 解析 + 标题文本显示 + 按键翻页 | ✅ 已完成 |
| **P1** | 集成 Bing 每日图片作为背景 | 📅 |
| **P2** | 缓存 + 离线浏览 | 📅 |
| **P3** | 低功耗定时刷新 + RTC 唤醒 | 📅 |
| **P4** | 多数据源支持 | 📅 |
