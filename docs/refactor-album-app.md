# AlbumApp 重构方案

## 现状

`AlbumApp` 单文件 921 行（`.cpp` 836 + `.h` 88），一个类承担所有职责，违反单一职责原则。

## 目标

拆分为 4 个独立模块，AlbumApp 精简为生命周期编排者（~300 行）。

## 模块设计

```
AlbumApp (~300 行)
  ├── ImageDownloader   →  HTTP 下载 + 重试
  ├── ImageRenderer     →  JPEG 解码 + Floyd-Steinberg 抖动 + EPD 输出
  ├── SlideShow         →  轮播逻辑 + 索引 + 日期管理
  └── PowerManager      →  休眠策略 + 活动跟踪
```

### 1. ImageDownloader

| 职责 | 来源（album_app.cpp） |
|------|----------------------|
| HTTP 下载 | `http_event_handler()`, `http_fetch_one()` |
| 单图下载+存 SD | `download_one()` |
| 批量刷新 | `refresh_all_images()` 的下载部分 |
| 无 SD 模式单图 | `fetch_and_show_one()` 的下载部分 |
| URL 管理 | 硬编码 URL，后续可支持多源 |

### 2. ImageRenderer

| 职责 | 来源 |
|------|------|
| JPEG 解码 | `decode_jpeg()` |
| 解码+渲染骨架 | `decode_and_render()` |
| 抖动输出 | `filter_and_display()` |
| 电池图标 | `draw_battery_icon()` |
| LED 指示 | `led_no_network()`, `led_failure()`, `led_success()` |

### 3. SlideShow

| 职责 | 来源 |
|------|------|
| 翻页 | `show_next()`, `show_prev()` |
| 自动翻页 | `check_auto_advance()` |
| 每日更新 | `check_daily_update()`, `get_today()` |
| 日期持久化 | `read_index_date()`, `write_index_date()` |
| SD 文件夹 | `ensure_album_folder()`, `scan_folder_images()` |
| 全部刷新 | `refresh_all_images()` |
| 延迟下载调度 | `run_pending_download()` |
| 加载显示 | `load_and_show()` |

### 4. PowerManager

| 职责 | 来源 |
|------|------|
| 空闲跟踪 | `s_last_activity_ms` |
| 休眠 | `go_to_sleep()` |
| 低电量检测 | 委托 `bat_is_low()` |

### 5. AlbumApp（精简后）

保留：
- `init()` — 初始化各模块、检测 RTC 唤醒、SD 状态
- `start() / stop() / refresh()` — 生命周期
- `update()` — 主线循环：下载调度 → 轮播 → 按钮 → 电源
- `handle_buttons()` — UP/DOWN/TOP 键处理

拆除：
- `deinit()` → 各模块各自清理

## 文件清单

| 文件 | 操作 | 预估行数 |
|------|------|---------|
| `apps/album/image_downloader.h` | 新建 | ~30 |
| `apps/album/image_downloader.cpp` | 新建 | ~100 |
| `apps/album/image_renderer.h` | 新建 | ~25 |
| `apps/album/image_renderer.cpp` | 新建 | ~120 |
| `apps/album/slide_show.h` | 新建 | ~40 |
| `apps/album/slide_show.cpp` | 新建 | ~220 |
| `apps/album/power_manager.h` | 新建 | ~25 |
| `apps/album/power_manager.cpp` | 新建 | ~70 |
| `apps/album/album_app.h` | 精简 | ~40 |
| `apps/album/album_app.cpp` | 精简 | ~300 |
| `main/CMakeLists.txt` | 添加源文件 | +4 行 |

## 依赖关系

```
AlbumApp
  ├── ImageDownloader ──── ImageRenderer (load_and_show → decode_and_render)
  ├── ImageRenderer
  │     └── LedDriver (LED 指示)
  ├── SlideShow
  │     ├── ImageDownloader (refresh → download)
  │     └── ImageRenderer (load_and_show → decode_and_render)
  └── PowerManager
        └── SlideShow (go_to_sleep 前保存索引)

AlbumApp 外部依赖（已有）
  ├── wifi_manager       — 连接、配网
  ├── wifi_provisioning  — 配网页面
  ├── hal                — HAL 层
  ├── led_driver         — LED
  └── config_file        — config.txt 读写
```

## 实施顺序

1. **ImageDownloader** — 独立模块，无其他模块依赖，可先提取
2. **ImageRenderer** — 独立模块，仅依赖 `led_driver` + `esp_new_jpeg`
3. **SlideShow** — 依赖 ImageDownloader + ImageRenderer
4. **PowerManager** — 依赖 SlideShow（休眠前保存索引）
5. **AlbumApp 精简** — 删除已提取代码，改用模块成员
