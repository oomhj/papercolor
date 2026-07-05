# 网络相册功能

在 PaperColor 墨水屏上滚动显示网络随机图片。

> 📌 当前实现：**P0**（固定 URL + 单图下载显示）

---

## 一、功能概述

| 项目 | 内容 |
|------|------|
| 应用目录 | `main/apps/album/` |
| 数据源 | picsum.photos 随机风景图片（600×400 JPEG） |
| 交互方式 | 3 个物理按键 |
| 依赖硬件 | EPD 显示屏、WiFi、RGB LED |
| 当前阶段 | P0 — 单图显示 + 按键翻页 |

---

## 二、文件结构

```
main/apps/album/
├── album_app.h         # 应用生命周期定义
└── album_app.cpp       # 主逻辑（WiFi + HTTP + 渲染 + 按键）

依赖:
- hal/hal (硬件抽象层)
- M5GFX (drawJpg, Canvas 离屏渲染)
- ESP-IDF: esp_http_client, esp_wifi, nvs_flash, esp-tls, mbedtls
```

---

## 三、按键交互

| 按键 | 功能 | 实现方式 |
|------|------|---------|
| BTN-A (G10) / BTN-C (G1) | 换一张新随机图片 | `_needs_refresh = true` |
| BTN-B (G9) | 刷新（重新下载当前图片） | `_needs_refresh = true` |
| BTN-C (G1) 长按 5 秒 | 进入深度休眠 | `pc_hal_deep_sleep()` |

所有按键均为 `wasClicked()` 触发换图；BTN-C 的 `wasHold()` 触发休眠。

---

## 四、数据流

```
按键 → LED 蓝灯 → WiFi 连接（硬编码） → HTTP GET → 动态缓冲区
  → LED 绿(成功)/红(失败) → drawJpg() → Canvas → EPD display()
  → LED 熄灭 → 等待
```

---

## 五、关键设计决策

| 决策 | 当前实现 | 未来规划 |
|------|---------|---------|
| 数据源 | picsum.photos 固定 URL | JSON 清单远程更新 |
| 布局 | 全屏 600×400 drawJpg | 缩放居中 + 底栏 |
| 动画 | 无（直接刷新整屏） | 步进滑动效果 |
| 缓存 | 无 | SD 卡离线缓存 |
| WiFi | 独立硬编码连接 | 迁移到 wifi_manager |

---

## 六、实施路线

| 阶段 | 内容 | 模块 | 状态 |
|------|------|------|------|
| P0 | Single URL 单图 + 按键翻页 | `album_app.cpp` | ✅ 已完成 |
| P1 | 图片缩放 + 状态栏 | `album_renderer.*` | 📅 |
| P2 | JSON 清单多图源 | album_app | 📅 |
| P3 | SD 卡缓存 | hal_storage | 📅 |

---

## 七、开发指引

- 修改数据源：编辑 `album_app.cpp` 中的 `IMAGE_URL` 和 `IMAGE_HOST`
- 修改 WiFi 配置：编辑文件顶部的 `ALBUM_SSID` / `ALBUM_PASS` 宏定义
- 入口文件：`main/main.cpp` 当前运行此应用

详细实现说明见 [`docs/album-design.md`](../../album-design.md)。
