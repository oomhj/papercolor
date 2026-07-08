# 配网流程

PaperColor 的 WiFi 配置方案 —— 使用 AP 热点 + Web 配网，不需要 App，不依赖屏幕交互。

---

## 一、核心思路

设备没有触屏，纯靠按键无法完成 WiFi 输入。因此配网通过**外部设备**（手机/电脑）连接设备 AP 热点后，在浏览器中完成。

```
用户操作                        PaperColor
─────────                      ──────────
                          开机 → NVS 检测 WiFi 配置？
                                        │
                           ┌── 无 ──────┼──── 有 ─────┐
                           │            │             │
                           │     进入应用   尝试连接 STA
                           │            │      ┌── 成功 ──┐
                           │            │      │         失败
                           │            │  正常运行  (album)
                           │            │              │
                    ─── 手机连接 PaperColor 热点 ──────┘
                    │
             打开浏览器 → 输入 192.168.4.1
                    │
              输入 WiFi 账号密码，点击扫描
                    │
              提交 → 保存到 NVS slot 0
                    │
              异步连接新 WiFi（不重启）
```

---

## 二、AP 热点配网流程

### 2.1 启动 AP

| 参数 | 值 |
|------|-----|
| SSID | `PaperColor-XXXX`（XXXX = MAC 后 3 位十六进制） |
| 密码 | 无（开放网络） |
| 信道 | 6 |
| 最大连接 | 4 |
| 触发时机 | UP+DOWN 同按 → SD wifi 读取失败 |

### 2.2 配网入口

用户需要手动输入 `http://192.168.4.1` 访问配网页。

> **旧版**：使用 DNS 劫持（UDP 53），所有请求重定向到配网页。  
> **当前**：不启动 DNS 服务器，用户需手动输入 IP。如需 DNS 劫持可自行添加。

### 2.3 配网页

单页 HTML，嵌入在 `wifi_provisioning.cpp` 中，支持 **PSK** 和 **802.1x Enterprise** 两种模式：

```
┌────────────────────────────────────┐
│  📶 PaperColor                    │
│                                    │
│  WiFi Name (SSID)                  │
│  [________________]                │
│                                    │
│  [toggle] 802.1x 企业认证          │
│          WPA2-Enterprise / PEAP    │
│                                    │
│  Username (EAP Identity)           │  ← enterprise 时显示
│  [User@domain.com]                 │
│                                    │
│  Password                          │
│  [________________]                │
│                                    │
│  [ 🔍 Scan Networks ]             │
│  [  Connect  ]                     │
│                                    │
│  Status: ...                       │
│                                    │
│  Hotspot: PaperColor-A84A          │
│  仅支持 2.4GHz 网络               │
└────────────────────────────────────┘
```

功能：
- **扫描**：点击"Scan Networks" → `GET /api/scan` → 列出附近 WiFi（RSSI + 加密标识）
- **选择**：点击扫描列表中的网络 → 自动填入 SSID 输入框
- **企业认证**：切换 toggle → 显示 Username (EAP Identity) 输入框
- **提交**：`POST /api/config` → 保存 NVS + SD → 异步连接

### 2.4 配网 API

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 配网页 HTML |
| `/api/scan` | GET | 扫描 WiFi（返回 JSON 列表） |
| `/api/config` | POST | 提交配置（PSK 或 Enterprise） |
| `/api/status` | GET | 连接状态 |
| `/*` | GET | 任意路径 → 配网页（兜底） |

**`POST /api/config` 请求体：**

PSK 模式：
```json
{"ssid": "MyNetwork", "pass": "MyPassword"}
```

Enterprise 模式：
```json
{"ssid": "CorpWiFi", "auth": "enterprise", "identity": "user@corp.com", "username": "user@corp.com", "pass": "password"}
```

**响应：**
```json
{"status": "ok", "message": "Saved successfully"}
```

**`GET /api/scan` 响应：**
```json
[{"ssid":"HomeNet", "rssi":-45, "secure":true}]
```

### 2.5 配网完成后流程

```
POST /api/config 成功后:
  1. 创建 deferred_connect_task (xTaskCreate)
  2. 返回 {"status":"ok"} 给浏览器
  3. deferred_connect_task:
     ├─ 等待 500ms（让浏览器收到响应）
     ├─ wifi_prov_stop() — 停止 HTTP 服务器
     ├─ wifi_mgr_connect_sta(10000) — 尝试连接新网络
     ├─ 等待 5s（让连接稳定）
     └─ led_off() — album 恢复正常运行
```

**与旧版不同**：不再重启设备。配网完成后异步连接 WiFi，用户无感知中断。

### 2.6 AP 自动关闭

`wifi_prov_tick()` 检测空闲超时：

| 参数 | 值 |
|------|-----|
| 空闲超时 | `AP_TIMEOUT_MS = 10 分钟` |
| 检测方式 | `now - last_activity > AP_TIMEOUT_MS` |
| last_activity | HTTP 请求时更新 |

**注意**：`wifi_prov_tick()` 需要外部调用。需要在 `AlbumApp::update()` 中集成。

---

## 三、物理按键触发配网

在 SD 模式下，用户可以通过按键触发配网：

```
操作: UP + DOWN 同按
  1. 尝试从 SD /sd/album/config.txt 读取 WiFi
  2. 连接 WiFi (5s 超时)
  3. 成功 → 保存 SD → 返回正常
  4. 失败 → led_async_breath_forever(yellow) → wifi_mgr_trigger_provisioning()
     └─ wifi_mgr_trigger_provisioning():
         ├─ esp_wifi_disconnect()
         ├─ wifi_mgr_start_ap()
         └─ wifi_prov_start()
```

---

## 四、SD 模式 WiFi 加载

AlbumApp 从 SD 读取 WiFi 配置，优先级：

```
1. /sd/album/config.txt (ssid=, pass=, auth=, identity=, username=)
   ↓ 不存在
2. /sd/wifi.txt (兼容旧版: 第1行=ssid, 第2行=pass, 第3行=dns)
```

保存时同时写入 NVS 和 SD：

```
保存 WiFi 到 SD:
  ├── /sd/album/config.txt (key=value 格式)
  │   ├── ssid=...
  │   ├── pass=...
  │   ├── auth=psk | enterprise
  │   ├── identity=...    (enterprise)
  │   └── username=...    (enterprise)
  └── NVS wifi/ssid_0, wifi/pass_0, wifi/auth_0, ...
```

---

## 五、多网络支持

NVS 中可保存**最多 3 组** WiFi 配置：

```
wifi/ssid_0, wifi/pass_0, wifi/auth_0    ← slot 0，首选
wifi/ssid_1, wifi/pass_1, wifi/auth_1    ← slot 1，备选
wifi/ssid_2, wifi/pass_2, wifi/auth_2    ← slot 2，备选
```

Enterprise 模式额外存储：
```
wifi/identity_0, wifi/username_0          ← EAP 参数
```

连接时按 slot 0→1→2 优先级逐一尝试：

```
for (每个 slot 0..2) {
    if (读取 NVS 成功) {
        if (auth == enterprise)
            → esp_eap_client_set_*() + PEAP
        else
            → PSK
        连接等待 timeout_ms
        if (连接成功) break
    }
}
```

---

## 六、状态机参考

详细状态机见 `connection-lifecycle.md`。

| 状态 | 枚举值 | 说明 |
|------|--------|------|
| OFF | `WIFI_STATE_OFF` | WiFi 未初始化 |
| STA_CN | `WIFI_STATE_STA_CN` | 连接中 |
| STA_OK | `WIFI_STATE_STA_OK` | 已连接（获得 IP） |
| STA_FAIL | `WIFI_STATE_STA_FAIL` | 连接失败 |
| STA_LOST | `WIFI_STATE_STA_LOST` | 运行时断线 |
| AP_IDLE | `WIFI_STATE_AP_IDLE` | AP 待机 |
| AP_CFG | `WIFI_STATE_AP_CFG` | AP 配置中 |

---

## 七、代码结构

| 文件 | 功能 |
|------|------|
| `wifi/wifi_manager.h/cpp` | STA/AP 管理、NVS 存储、事件处理、LED 状态 |
| `wifi/wifi_provisioning.h/cpp` | HTTP 服务器、配网页 HTML、WiFi 扫描 |
| `hal/config_file.h/cpp` | key=value 文件读写（通用模块） |

---

## 八、LED 状态指示

| 状态 | LED | 说明 |
|------|-----|------|
| OFF / INIT | 熄灭 | 硬件初始化 |
| STA 连接中 (STA_CN) | 🔵 蓝灯呼吸 | 正在连接路由器 |
| STA 已连接 (STA_OK) | 🟢 绿灯常亮 | 连接成功 |
| STA 断开 (STA_LOST) | 🟠 橙灯闪烁 | 运行时断线 |
| STA 失败 (STA_FAIL) | 🔴 红灯闪烁 3 次 | 全部重试失败 |
| AP 开启 (AP_IDLE) | 🟡 黄灯闪烁 | 配网模式，等待客户端 |
| AP 配置中 (AP_CFG) | 🔵 青灯常亮 | 客户端已连接，正在配网 |
| 休眠 | 熄灭 | 深度睡眠 |
