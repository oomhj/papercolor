# 联网及配网功能

PaperColor 的网络连接与配置方案 — 完整 WiFi 管理模块，AlbumApp 已集成。

---

## 一、功能概览

| 功能 | 状态 | 说明 |
|------|------|------|
| STA 连接 | ✅ | 自动重试，NVS 存储最多 3 组网络 |
| AP 配网 | ✅ | Web 页面，无需手机 App |
| 802.1x Enterprise | ✅ | WPA2-Enterprise / PEAP-MSCHAPv2 |
| LED 状态指示 | ✅ | 7 种 LED 效果对应不同状态 |
| SD 模式 WiFi 预配置 | ✅ | 从 SD 读取 WiFi 配置 |
| 配网不重启 | ✅ | deferred_connect_task 异步连接 |

---

## 二、方案

```
WiFi 管理
  ├── STA 模式 (wifi_manager)
  │   ├── NVS 存储 (最多 3 组 PSK / Enterprise)
  │   ├── 按优先级逐一尝试
  │   └── LED 状态映射
  │
  ├── AP 模式 (wifi_provisioning)
  │   ├── HTTP 配网页 (单页 HTML，嵌入式)
  │   ├── WiFi 扫描 API
  │   └── 10 分钟空闲自动关闭 (需集成 wifi_prov_tick)
  │
  └── 配网触发
      └─ AlbumApp: UP+DOWN 同按 → 读 SD → 失败则 AP 配网
```

---

## 三、存储方案

### NVS 存储（最多 3 组）

| Key 格式 | 类型 | 说明 |
|----------|------|------|
| `ssid_{0..2}` | string | WiFi 名称 |
| `pass_{0..2}` | string | WiFi 密码 |
| `auth_{0..2}` | string | `psk` 或 `enterprise` |
| `identity_{0..2}` | string | EAP Identity（Enterprise 用） |
| `username_{0..2}` | string | EAP Username（Enterprise 用） |

### SD 存储（AlbumApp）

```
/sd/album/config.txt:
  ssid=MyNetwork
  pass=MyPassword
  auth=psk
  identity=...    # enterprise
  username=...    # enterprise
```

加载优先级：`config.txt` → `wifi.txt`（兼容旧版）

---

## 四、连接流程

```
AlbumApp 启动:
  ├─ wifi_mgr_init() → NVS + netif + event loop
  ├─ sd 有 WiFi 配置?
  │   └─ YES → load → wifi_mgr_save_network() → NVS
  └─ 进入主循环

主循环:
  ├─ wifi_mgr_get_state() == STA_OK → HTTP 可用
  ├─ 不是 → 需要 WiFi 时调用 wifi_ensure_connected(5000)
  └─ UP+DOWN → 读 SD WiFi → 失败 → AP 配网

配网流程:
  wifi_mgr_trigger_provisioning():
    ├─ esp_wifi_disconnect()
    ├─ wifi_mgr_start_ap()
    └─ wifi_prov_start()
        └─ HTTP 服务器 + 配网页

配网完成:
  POST /api/config → save NVS + SD
    └─ deferred_connect_task:
        ├─ wait 500ms
        ├─ wifi_prov_stop()
        ├─ wifi_mgr_connect_sta(10000)
        └─ led_off() → album 恢复
```

---

## 五、模块结构

```
main/wifi/
├── wifi_manager.h/cpp      # STA/AP 管理、NVS、事件处理、LED
└── wifi_provisioning.h/cpp # HTTP 服务器、配网页、WiFi 扫描
```

---

## 六、API 参考

```c
// 初始化
void wifi_mgr_init(void);

// 连接
bool wifi_mgr_connect_sta(uint32_t timeout_ms);
void wifi_mgr_disconnect_sta(void);

// AP 配网
void wifi_mgr_start_ap(void);
void wifi_mgr_stop_ap(void);
void wifi_mgr_trigger_provisioning(void);  // 断开 STA → 启动 AP + provisioning

// 状态
wifi_state_t wifi_mgr_get_state(void);
const char* wifi_mgr_get_ip(void);
const char* wifi_mgr_get_ap_ssid(void);
void wifi_mgr_update_led(void);

// NVS 存储
bool wifi_mgr_save_network(int slot, const char* ssid, const char* pass);
bool wifi_mgr_save_network_ext(int slot, const char* ssid, const char* auth, ...);
bool wifi_mgr_load_network(int slot, char* ssid, size_t ssid_sz, char* pass, size_t pass_sz);
void wifi_mgr_erase_all(void);

// 配网
void wifi_prov_start(void);
void wifi_prov_stop(void);
void wifi_prov_tick(void);       // 空闲超时检测（需外部调用）
void wifi_save_config_to_sd(void);  // NVS slot 0 → SD config.txt
```

---

## 七、状态机

详细状态机见 `connection-lifecycle.md`。

```
OFF → STA_CN → STA_OK ──(runtime disconnect)──→ STA_LOST
       ↓               ↓
    STA_FAIL      (正常)
       ↓
    AP_IDLE → AP_CFG → 提交配置 → deferred_connect → STA_CN
```

---

## 八、LED 状态总表

| 状态 | LED | 说明 |
|------|-----|------|
| OFF / INIT | 熄灭 | 系统启动中 |
| STA_CN | 🔵 蓝灯呼吸 | 正在连接 |
| STA_OK | 🟢 绿灯常亮 | 已连接 |
| STA_LOST | 🟠 橙灯闪烁 | 运行时断线 |
| STA_FAIL | 🔴 红灯闪烁 3 次 | 全部重试失败 |
| AP_IDLE | 🟡 黄灯闪烁 | 配网等待 |
| AP_CFG | 🔵 青灯常亮 | 客户端配置中 |
