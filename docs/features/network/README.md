# 联网及配网功能

PaperColor 的网络连接与配置方案 — 当前已实现完整 WiFi 管理模块。

---

## 一、需求

1. **WiFi 联网** — 连接指定或保存的路由器 (STA 模式)
2. **配置方式** — AP 热点 + Web 页面配网，无需手机 App
3. **多网络支持** — NVS 中保存最多 3 组 WiFi 配置，按优先级尝试
4. **断线重连** — 自动检测断线，退避重试（最多 3 次后进入配网模式）
5. **配网触发** — 长按 BTN-A 3 秒手动进入配网模式
6. **LED 反馈** — 呼吸灯效果指示当前连接状态

---

## 二、方案选型回顾

| 方案 | 描述 | 选型 |
|------|------|------|
| 编译时硬编码 | SSID/PASS 写在 #define 中 | P0 过渡方案（news/album 仍用） |
| ESP-IDF WiFi Provisioning | 通过 BLE/SoftAP 配网 | 未采用（需手机 App） |
| **Web 配网** | **AP 热点 + DNS 劫持 + HTTP 配网页** | **✅ 已实现** |
| 串口配置 | 通过 USB 串口输入 WiFi 信息 | 未采用（需连接电脑） |

---

## 三、存储方案

WiFi 配置存储在 NVS 中，支持多组：

| Key 格式 | 类型 | 说明 |
|----------|------|------|
| `wifi_ssid_{0..2}` | string | WiFi 名称（最多 32 字符） |
| `wifi_pass_{0..2}` | string | WiFi 密码（最多 64 字符） |

首次启动检测 NVS → 如果有保存的 WiFI → 自动尝试连接
全部连接失败 → 启动 AP 热点等待配网

---

## 四、连接流程

```
应用启动
    │
    ├─ wifi_mgr_init()
    │   ├─ NVS 有 WiFi 配置？
    │   │   ├─ YES → wifi_mgr_start_retry_loop(false)
    │   │   │           ├─ 按优先级尝试保存的网络（最多 3 次）
    │   │   │           ├─ 每次等待间隔递增（2s → 5s → 15s）
    │   │   │           ├─ 成功 → WIFI_STATE_STA_OK，LED 绿 0.5s
    │   │   │           │
    │   │   │           └─ 全部失败 → WIFI_STATE_STA_FAIL
    │   │   │                       ├─ 启动 AP 热点
    │   │   │                       ├─ 启动 DNS 劫持 + HTTP 配网页
    │   │   │                       └─ 3 分钟后自动关闭 AP
    │   │   │
    │   │   └─ NO  → 启动 AP 配网（同上）
    │   │
    │   └─ 运行时断线 → wifi_mgr_start_retry_loop(true)
    │                    ├─ 立即重试 → 5s → 15s
    │                    └─ 全部失败 → 启动 AP
    │
    LED: 连接中 → 蓝灯慢速呼吸（2s 周期，亮度递减表示重试次数）
         成功   → 绿灯常亮 3 秒后熄灭
         失败   → 红灯 2s
         配网中 → 蓝灯快速呼吸（500ms 周期，快闪）
```

---

## 五、模块结构

```
main/wifi/
├── wifi_manager.h          # 统一 API：STA + AP 模式管理、重试退避
├── wifi_manager.cpp        # 实现：状态机、LED 呼吸灯、按键检测
├── wifi_provisioning.h     # 配网服务器 API
├── wifi_provisioning.cpp   # 实现：DNS 劫持 + HTTP 服务器 + 配网页
└── www/                    # 配网页静态资源（可选）
```

---

## 六、当前状态

| 模块 | 状态 | 说明 |
|------|------|------|
| wifi_manager | ✅ 已实现 | 完整 STA/AP 管理、NVS 存储、重试退避、LED 反馈 |
| wifi_provisioning | ✅ 已实现 | DNS 劫持、WiFi 扫描、Web 配网页、3 分钟自动关闭 |
| P0 应用集成 | ⏳ 待集成 | news/album 目前使用独立的硬编码 WiFi 连接 |

P0 应用（news/album）尚未切换到 wifi_manager。`news_app.cpp` 和 `album_app.cpp` 各自包含独立的 `wifi_connect()` 实现，使用硬编码的 SSID/PASS（编译时定义）。后续将统一迁移到 wifi_manager。
