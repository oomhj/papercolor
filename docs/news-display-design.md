# 热点图片新闻 — 设计方案

在 PaperColor 墨水屏上轮播热点新闻，支持图片+标题显示。

---

## 一、整体架构

```
┌──────────────┐     HTTP/TLS      ┌───────────────────┐
│   News API   │ ◄──────────────►  │  PaperColor       │
│  (云端)      │                   │  ┌─────────────┐  │
│              │                   │  │ news_app     │  │
│ • Bing 每日  │                   │  │  ├ fetcher   │  │
│ • NewsAPI    │                   │  │  ├ parser    │  │
│ • RSS 聚合   │                   │  │  └ renderer  │  │
│ • 自建服务   │                   │  │  hal_wifi    │  │
└──────────────┘                   │  └─────────────┘  │
                                   └───────────────────┘
```

## 二、数据源方案

### 方案 A：Bing 每日一图（推荐首选）

| 项目 | 内容 |
|------|------|
| API | `https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN` |
| 格式 | JSON |
| 返回 | 图片 URL + 标题 + 版权说明 |
| 费用 | 免费，无需 API Key |
| 图片 | 1920x1080 JPEG，约 200-500KB |
| 局限 | 每天一张，不含"热点新闻" |

### 方案 B：NewsAPI

| 项目 | 内容 |
|------|------|
| API | `https://newsapi.org/v2/top-headlines?country=cn&apiKey=KEY` |
| 格式 | JSON |
| 返回 | 标题、描述、来源、图片 URL |
| 费用 | 免费版 100 req/day |
| 需要 | API Key（免费注册） |

### 方案 C：RSS 聚合

| 项目 | 内容 |
|------|------|
| 源 | 新浪新闻、BBC、Reuters 等 RSS feed |
| 格式 | XML (RSS/Atom) |
| 处理 | 解析 XML 提取标题+链接 |
| 需要 | 简单的 XML 解析器（可用 expat 或 minixml） |
| 优点 | 免费、无限制、无需 API Key |

### 方案 D：GitHub Raw 自建

| 项目 | 内容 |
|------|------|
| 方式 | 在 GitHub 仓库维护 `news.json`，设备定时拉取 |
| 格式 | 自定义 JSON（标题+图片URL+日期） |
| 优点 | 完全可控、内容自定义 |
| 缺点 | 需要手动更新 |

### 推荐：方案 C（RSS）+ 方案 A（Bing 图片）组合

首次实现先从 **RSS** 获取标题，配合 **Bing 每日一图** 做背景，无需处理第三方 API Key。

---

## 三、应用结构

### 新模式：`APP_MODE_NEWS`（mode_4）

继承 demo 的模式系统，与 calendar、local_slideshow 平级。

```
main/apps/news/
├── news_app.h              # 应用生命周期
├── news_app.cpp            # 主逻辑
├── news_fetcher.h          # HTTP 获取接口
├── news_fetcher.cpp        # esp_http_client 实现
├── news_parser.h           # RSS/JSON 解析
├── news_parser.cpp         # 解析实现
└── news_renderer.h/cpp    # EPD 渲染
```

### 数据流

```
定时器到期
    │
    ▼
WiFi 连接 STA
    │
    ▼
news_fetcher 发起 HTTP GET
    │
    ▼
news_parser 解析响应
    │
    ▼
news_renderer 绘制到 Canvas
    │
    ▼
EPD 局部刷新 (epd_fastest)
    │
    ▼
用户按键翻页 / 下一周期更新
```

---

## 四、EPD 显示布局

横屏 600×400，每页显示一条新闻：

```
┌──────────────────────────────────────────────────────────────────┐
│ 🔥  Hot News                          2026-07-03    1/10  ██   │  ← 标题栏
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│              ┌──────────────────────┐                            │
│              │                      │                            │
│              │   新闻配图区域          │                            │  ← 图片区域
│              │   (如无图则纯色)       │                            │    280×180
│              │                      │                            │
│              └──────────────────────┘                            │
│                                                                  │
│  ┌──────────────────────────────────────────────────────┐        │
│  │  神舟二十号飞船成功发射 搭载三名航天员进入太空         │        │  ← 标题
│  └──────────────────────────────────────────────────────┘        │
│                                                                  │
│  来源: 新华社  |  2小时前                                        │  ← 元信息
│                                                                  │
│  2026年7月3日，神舟二十号载人飞船在酒泉卫星发射中心...           │  ← 摘要(2行)
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  [A] ◄ Prev    [B] Refresh    [C] Next ►        Auto: 30min    │  ← 底部栏
└──────────────────────────────────────────────────────────────────┘
```

### 关键尺寸

| 区域 | 位置 | 大小 |
|------|------|------|
| 标题栏 | y=0 | h=36 |
| 图片区 | y=40～290 | 中心 280×180 |
| 标题文字 | y=300 | 2 行 Font4 |
| 元信息 | y=340 | Font0 |
| 摘要 | y=360 | Font2, 2 行 |
| 底部栏 | y=380 | h=20 |

---

## 五、关键实现细节

### 5.1 HTTP 获取（news_fetcher.cpp）

```cpp
// 使用 ESP-IDF esp_http_client
esp_http_client_config_t cfg = {};
cfg.url = "https://...";
cfg.timeout_ms = 15000;
cfg.buffer_size = 4096;

esp_http_client_handle_t client = esp_http_client_init(&cfg);
esp_http_client_perform(client);

// 读取响应到内存缓冲区
char* response = malloc(content_length + 1);
esp_http_client_read(client, response, content_length);
response[content_length] = '\0';
```

### 5.2 RSS 解析（news_parser.cpp）

RSS 格式：
```xml
<rss>
  <channel>
    <item>
      <title>新闻标题</title>
      <description>摘要内容</description>
      <pubDate>发布时间</pubDate>
      <enclosure url="图片URL" type="image/jpeg"/>
    </item>
  </channel>
</rss>
```

解析方案：使用 **ESP-IDF 自带的 expat XML 解析器**，或**简单字符串解析**（RSS 结构固定）。

### 5.3 图片渲染（news_renderer.cpp）

M5GFX 支持直接绘制 PNG/JPEG：

```cpp
// 从内存绘制 PNG
g_canvas->drawPng(data, len, x, y, w, h, 0, 0, scale);

// 从文件绘制 PNG
g_canvas->drawPngFile(path, x, y, w, h, 0, 0, scale);
```

图片处理流水线：
1. HTTP 下载 JPEG → 内存缓冲区
2. 如有必要缩放至 280×180
3. 居中绘制到 Canvas
4. 叠加标题文字

### 5.4 低功耗策略

- 使用 `esp_timer` 定时唤醒（默认 30 分钟）
- 唤醒后：连接 WiFi → 获取数据 → 刷新 EPD → 断开 WiFi → 休眠
- 借鉴 demo 的 RTC 唤醒逻辑（`scheduleNextWakeMinutes`）

### 5.5 缓存策略

- 最后 N 条新闻保存在内存数组（N=50）
- 离线时显示缓存内容
- 每次新获取追加到缓存

---

## 六、集成步骤

### 6.1 新增文件

```
main/apps/news/news_app.h
main/apps/news/news_app.cpp
main/apps/news/news_fetcher.h
main/apps/news/news_fetcher.cpp
main/apps/news/news_parser.h  
main/apps/news/news_parser.cpp
main/apps/news/news_renderer.h
main/apps/news/news_renderer.cpp
```

### 6.2 修改文件

| 文件 | 修改内容 |
|------|----------|
| `hal/hal.h` | 添加 `MODE_ID_NEWS` / `APP_MODE_NEWS` |
| `hal/hal.cpp` | 更新 `is_supported_mode_id` / `app_mode_from_mode_id` |
| `app_manager/app_manager.cpp` | 集成 news_app 的 init/start/stop/update |
| `app_server/app_server.cpp` | 添加 `"NEWS"` 到模式列表 |
| `CMakeLists.txt` | 添加 news 源文件 |
| `idf_component.yml` | - 无需新增依赖（esp_http_client 已内置） |

### 6.3 CMakeLists.txt 依赖

```cmake
REQUIRES
    ...
    esp_http_client
    esp-tls
    json
```

> 注意：以上组件已在 demo 的 CMakeLists.txt 中启用，无需额外配置。

---

## 七、实施路线

| 阶段 | 内容 | 时间估计 |
|------|------|----------|
| **P0** | RSS 解析 + 标题文本显示 | 1 天 |
| **P1** | 集成 Bing 每日图片作为背景 | 1 天 |
| **P2** | 缓存 + 离线浏览 | 0.5 天 |
| **P3** | 低功耗定时刷新 + RTC 唤醒 | 0.5 天 |
| **P4** | 多数据源支持（NewsAPI 等） | 1 天 |

---

## 八、风险与应对

| 风险 | 影响 | 应对 |
|------|------|------|
| RSS 源格式变更 | 解析失败 | 设计容错解析，降级显示纯文本 |
| 图片过大（>1MB） | OOM | 限制下载大小，丢弃超大图片 |
| WiFi 连接慢 | 刷新耗时过长 | 超时机制，失败时显示缓存内容 |
| 多次刷新减损 EPD 寿命 | 硬件损伤 | 最小刷新间隔 5 分钟，仅内容变化时刷新 |
| HTTPS 证书验证 | 连接失败 | 配置 esp-tls 跳过验证（开发阶段） |
