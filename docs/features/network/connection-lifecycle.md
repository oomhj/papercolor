# 连接生命周期管理

WiFi 连接的状态机、重试、失败处理、手动触发配网 — 当前实现说明。

---

## 一、连接状态机

```
         ┌──────────┐
         │  OFF     │  WiFi 未初始化
         └────┬─────┘
              │ wifi_mgr_init()
         ┌────▼─────┐
         │  INIT    │  ← 系统启动，注册事件处理器
         └────┬─────┘
              │ wifi_mgr_connect_sta()
         ┌────▼─────┐
         │ STA_CN   │  STA 连接中（按优先级尝试保存的网络）
         └────┬─────┘
              │
        ┌─────┴────────┐
        │              │
    ┌───▼───┐    ┌────▼────┐
    │STA_OK │    │STA_FAIL │  ← 全部尝试失败
    └───┬───┘    └────┬────┘
        │             │
    ┌───▼───┐    ┌────▼────┐
    │STA_LOST│    │AP_IDLE  │  ← AP 热点已开启
    └───┬───┘    └────┬────┘
        │             │ 手机连接热点
        │         ┌────▼────┐
        │         │ AP_CFG  │  ← 用户在配网页操作
        │         └────┬────┘
        │              │ 提交 WiFi 配置
        │         ┌────▼────┐
        └─ 重连 ─┤STA_CN   │  ← deferred_connect_task
                 └─────────┘
```

**状态说明**：

| 状态 | 枚举值 | 含义 |
|------|--------|------|
| `WIFI_STATE_OFF` | 0 | WiFi 未初始化 |
| `WIFI_STATE_STA_CN` | 1 | STA 连接中 |
| `WIFI_STATE_STA_OK` | 2 | STA 已连接（获得 IP） |
| `WIFI_STATE_STA_FAIL` | 3 | STA 全部重试失败 |
| `WIFI_STATE_STA_LOST` | 4 | 运行时断线（曾 OK，现在丢失） |
| `WIFI_STATE_AP_IDLE` | 5 | AP 热点开启，等待客户端 |
| `WIFI_STATE_AP_CFG` | 6 | AP 配置中（客户端已连接） |

---

## 二、连接策略

### 2.1 STA 连接流程

`wifi_mgr_connect_sta(timeout_ms)` 按优先级遍历 NVS 中保存的网络槽位：

```
for slot 0..WIFI_SAVED_NETS-1:
    load NVS (ssid, pass, auth, identity...)
    set STA config (PSK 或 Enterprise)
    connect
    wait up to timeout_ms
    if GOT_IP → STA_OK → return true
    if DISCONNECTED → STA_FAIL → try next slot
```

**超时建议**：
- PSK: 5-10 秒通常足够
- Enterprise: 可能需要 10-15 秒

调用方控制超时：
- `album_app.cpp` 中 `wifi_ensure_connected()`: 5 秒
- `wifi_provisioning.cpp` 中 `deferred_connect_task`: 10 秒

### 2.2 运行时断线处理

WiFi 断开事件（`WIFI_EVENT_STA_DISCONNECTED`）触发状态从 `STA_OK` 变为 `STA_LOST`，LED 变为橙色闪烁。

当前实现中，AlbumApp 不自动重连——需要用户操作（UP+DOWN 重新配网或重启）。

### 2.3 呼吸灯效果

连接过程中 LED 使用呼吸灯效果：

| 状态 | LED | 说明 |
|------|-----|------|
| STA 连接中 (STA_CN) | 蓝灯慢速呼吸（2s 周期） | 正在连接路由器 |
| STA 已连接 (STA_OK) | 绿灯常亮 | 连接成功 |
| STA 断开 (STA_LOST) | 橙灯闪烁 | 运行时断线 |
| STA 失败 (STA_FAIL) | 红灯闪烁 3 次 | 全部重试失败 |

---

## 三、手动触发配网

用户可以通过按键进入配网模式：

### 操作方式

| 操作 | 当前状态 | 结果 |
|------|----------|------|
| **UP + DOWN 同按** | SD 模式 | 尝试 SD wifi.txt → 失败则启动 AP 配网 |

### 配网触发流程

```
BTN_UP + BTN_DOWN 同按:
  1. 尝试从 SD /sd/album/config.txt 读取 WiFi 配置
  2. 连接 WiFi (5s 超时)
  3. 成功 → 保存 → 返回
  4. 失败 → led_async_breath_forever(yellow) → wifi_mgr_trigger_provisioning()
```

### LED 反馈

| 操作 | LED 效果 |
|------|----------|
| UP+DOWN 同按 | 蓝灯闪烁 |
| 配网模式启动 | 黄灯闪烁（`WIFI_STATE_AP_IDLE`） |
| 客户端连接 AP | 青灯常亮（`WIFI_STATE_AP_CFG`） |

---

## 四、配网完成后的行为

用户通过配网页提交 WiFi 配置后：

```
POST /api/config:
  1. 解析 JSON (ssid, pass, auth, identity, username)
  2. 保存到 NVS slot 0
  3. 保存到 SD /sd/album/config.txt (wifi_save_config_to_sd)
  4. 返回 {"status":"ok"}

deferred_connect_task:
  1. 等待 500ms（让浏览器收到响应）
  2. wifi_prov_stop() — 停止 HTTP + DNS
  3. wifi_mgr_connect_sta(10000) — 尝试连接新网络
  4. 等待 5s → led_off() → album 恢复运行
```

**与旧版不同**：不重启设备。配网完成后异步连接 WiFi，用户无感知中断。

---

## 五、AP 自动关闭

`wifi_prov_tick()` 检查 AP 空闲超时：

```
AP_TIMEOUT_MS = 10 min
if (now - last_activity > AP_TIMEOUT_MS):
    wifi_prov_stop()
    wifi_mgr_stop_ap()
```

**注意**：`wifi_prov_tick()` 当前未被调用。需要在 AlbumApp::update() 中集成：

```cpp
void AlbumApp::update() {
    // ... existing code ...
    if (ws == WIFI_STATE_AP_IDLE || ws == WIFI_STATE_AP_CFG) {
        wifi_prov_tick();  // ← 添加此调用
        return;
    }
    // ...
}
```

---

## 六、API 参考

### wifi_manager

```c
void wifi_mgr_init(void);                     // 初始化 WiFi + NVS + 事件处理
bool wifi_mgr_connect_sta(uint32_t timeout_ms); // 连接 STA（遍历 NVS 槽位）
void wifi_mgr_disconnect_sta(void);           // 断开 STA

void wifi_mgr_start_ap(void);                 // 启动 AP 热点
void wifi_mgr_stop_ap(void);                  // 停止 AP 热点
void wifi_mgr_trigger_provisioning(void);      // 断开 STA → 启动 AP + provisioning

wifi_state_t wifi_mgr_get_state(void);        // 当前状态
const char* wifi_mgr_get_ip(void);            // STA IP ("0.0.0.0" 如果未连接)
const char* wifi_mgr_get_ap_ssid(void);       // AP SSID
void wifi_mgr_update_led(void);               // 根据状态设置 LED

// NVS 存储
bool wifi_mgr_save_network(int slot, const char* ssid, const char* pass);
bool wifi_mgr_load_network(int slot, char* ssid, ...);
bool wifi_mgr_save_network_ext(int slot, const char* ssid, const char* auth,
                               const char* identity, const char* username,
                               const char* pass);
void wifi_mgr_erase_all(void);
```

### wifi_provisioning

```c
void wifi_prov_start(void);     // 启动 HTTP 服务器（配网页）
void wifi_prov_stop(void);      // 停止 HTTP 服务器
void wifi_prov_tick(void);      // 检查空闲超时（需外部调用）
void wifi_save_config_to_sd(void);  // NVS slot 0 → SD config.txt
```

### HTTP API 端点

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 配网页 HTML |
| `/api/scan` | GET | 扫描 WiFi（返回 JSON 列表） |
| `/api/config` | POST | 提交配置 `{"ssid":"...", "pass":"..."}` 或 enterprise |
| `/api/status` | GET | 连接状态 |

---

## 七、LED 状态总表

| 状态 | LED | 触发 |
|------|-----|------|
| OFF / INIT | 熄灭 | 系统启动中 |
| STA 连接中 (STA_CN) | 🔵 蓝灯慢速呼吸 | 等待连接 |
| STA 已连接 (STA_OK) | 🟢 绿灯常亮 | 连接成功 |
| STA 断开 (STA_LOST) | 🟠 橙灯闪烁 | 运行时断线 |
| STA 失败 (STA_FAIL) | 🔴 红灯闪烁 3 次 | 全部重试失败 |
| AP 待机 (AP_IDLE) | 🟡 黄灯闪烁 | AP 开启，等待客户端 |
| AP 配置中 (AP_CFG) | 🔵 青灯常亮 | 客户端已连接，正在配网 |
| 休眠 | 熄灭 | 深度睡眠 |
