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
                           │     启动 AP 热点     尝试连接 STA
                           │     + DNS 劫持         │
                           │            │      ┌── 成功 ──┐
                           │            │      │         失败
                           │            │  进入应用   启动 AP
                           │            │           + DNS 劫持
                           │            │              │
                    ─── 手机连接 PaperColor 热点 ──────┘
                    │
             打开浏览器 → 自动弹出配网页
                    │
              输入 WiFi 账号密码，点击扫描
                    │
              提交 → 保存到 NVS slot 0
                    │
              设备重启 → 自动连接 STA
```

---

## 二、AP 热点配网流程

### 2.1 启动 AP

| 参数 | 值 |
|------|-----|
| SSID | `PaperColor-XXXX`（XXXX = MAC 后 4 位十六进制） |
| 密码 | 无（开放网络） |
| 信道 | 6 |
| 最大连接 | 4 |
| 触发时机 | STA 重试全部失败、或用户长按 BTN-B 3 秒 |

### 2.2 DNS 劫持

启动 DNS 服务器（UDP 53），将所有域名请求解析到 `192.168.4.1`。用户连接热点后打开任意网址都会弹出配网页（Captive Portal 效果）。

### 2.3 配网页

单页 HTML，嵌入在 `wifi_provisioning.cpp` 中：

```
┌────────────────────────────────────┐
│  PaperColor WiFi Setup            │
│                                    │
│  SSID:  [________________]  [扫描]│
│  Password: [________________]      │
│                                    │
│  [        连接 WiFi       ]        │
│                                    │
│  Status: Waiting...                │
│  ─────────────────────────         │
│  Hotspot: PaperColor-A84A          │
│                                    │
│  提示: 仅支持 2.4GHz WiFi          │
└────────────────────────────────────┘
```

功能：
- 点击"扫描"按钮 → `GET /api/scan` → 列出附近 WiFi 网络（含 RSSI 和加密标识）
- 点击扫描列表中的网络 → 自动填入 SSID 输入框
- 提交表单 → `POST /api/config` → 保存到 NVS → 重启

### 2.4 配网 API

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 配网页 HTML |
| `/api/scan` | GET | 扫描附近 WiFi（返回 JSON 列表） |
| `/api/config` | POST | 提交 WiFi 配置 `{"ssid":"...", "pass":"..."}` |
| `/api/status` | GET | 查询连接状态 |

**`POST /api/config` 响应：**
```json
{"status": "ok", "message": "Connecting..."}
```

**`GET /api/scan` 响应：**
```json
[{"ssid":"HomeNet", "rssi":-45, "secure":true}]
```

**`GET /api/status` 响应：**
```json
{"connected": true, "ip": "192.168.4.1"}
```

### 2.5 AP 自动关闭

| 条件 | 行为 |
|------|------|
| STA 连接成功后 | AP 保持 3 分钟供用户配置 |
| 3 分钟无操作 | 关闭 AP + DNS + HTTP |
| 用户提交配网 | 保存配置后立即重启 |
| "/*" 路由 | 所有 GET 请求（含 `/generate_204` 等）返回配网页 |

---

## 三、物理按键触发配网

任何时候，用户可以通过按键重新进入配网模式：

```
当前状态                    操作
─────────                  ──
任何状态                   长按 BTN-B (G9) 3 秒
                            → 蓝灯亮
                            → 3 秒到，绿灯亮 200ms
                            → 断开当前 STA
                            → 启动 AP 热点
                            → 启动配网服务器
                            → 用户可重新配置 WiFi
```

这样换网络时不需要重新编译烧录。

---

## 四、多网络支持

NVS 中可保存**多组** WiFi 配置（最多 3 组）：

```
wifi/ssid_0, wifi/pass_0    ← slot 0，默认首选
wifi/ssid_1, wifi/pass_1    ← slot 1，备选
wifi/ssid_2, wifi/pass_2    ← slot 2，备选
```

连接时按 slot 0→1→2 优先级逐一尝试：

```
for (每个 slot 0..2) {
    if (读取 NVS 成功) {
       设置 SSID/PASS
       连接等待 timeout_ms
       if (连接成功) break
    }
}
```

全部失败则进入 AP 配网模式。

---

## 五、状态机参考

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

## 六、代码结构

| 文件 | 功能 |
|------|------|
| `wifi/wifi_manager.h/cpp` | STA/AP 管理、NVS 存储、重试退避 |
| `wifi/wifi_provisioning.h/cpp` | DNS 劫持、HTTP 服务器、配网页 |
| `wifi/www/` | 配网页静态资源 |

---

## 七、LED 状态指示

| 状态 | LED 效果 | 说明 |
|------|----------|------|
| 启动中 | 熄灭 | 硬件初始化 |
| STA 连接中 | 蓝灯慢速呼吸（2s 周期） | 正在连接路由器，亮度递减表示重试次数 |
| STA 已连接 | 绿灯常亮 3 秒后熄灭 | 连接成功指示 |
| STA 断线 | 橙灯 | 运行时断线 |
| STA 全部失败 | 红灯 | 重试全部失败 |
| AP 开启 | 蓝灯快速呼吸（500ms 周期快闪） | 配网模式，等待客户端连接 |
| AP 配置中 | 青灯常亮 | 客户端已连接，有人在操作配网页 |
| 休眠 | 熄灭 | 深度睡眠 |
