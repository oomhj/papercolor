# 联网及配网功能

PaperColor 的网络连接与配置方案。

---

## 一、需求

1. **WiFi 联网** — 连接指定路由器 (STA 模式)
2. **配置方式** — 用户能方便地修改 WiFi 账号密码
3. **连接状态** — 实时反馈联网进度与结果
4. **多网络支持** — 同时开启 AP 热点供 Web 配置
5. **断线重连** — 自动检测断线并恢复

---

## 二、方案对比

### 方案 A：编译时硬编码（当前做法）

```
#define NEWS_SSID "Jason-home"
#define NEWS_PASS "admin1234"
```
- 优点：简单，无需交互
- 缺点：每次换网络要重新编译烧录

### 方案 B：WiFi  provisioning（ESP-IDF 官方方案）

使用 `wifi_provisioning` 组件，手机 App 通过 BLE/SoftAP 配网。
- 优点：标准方案，用户体验好
- 缺点：需要手机 App，实现复杂

### 方案 C：Web 配网（m5_demo 做法）

设备启动 AP 热点 → 手机连接 → 打开网页 → 输入 WiFi 信息 → 保存到 NVS。
- 优点：无需 App，浏览器即可配置
- 缺点：需要实现 DNS 劫持 + HTTP 服务器

### 方案 D：串口配置

通过 USB 串口输入 WiFi 信息。
- 优点：实现简单
- 缺点：需要连接电脑

### 推荐方案

**P0 用方案 A（硬编码）+ 方案 D（串口辅助）**
**P1 升级到方案 C（Web 配网）**

---

## 三、存储方案

WiFi 信息保存在 NVS (Non-Volatile Storage) 中：

| Key | 类型 | 说明 |
|-----|------|------|
| `wifi_ssid` | string(32) | WiFi 名称 |
| `wifi_pass` | string(64) | WiFi 密码 |
| `wifi_mode` | uint8 | 0=未配置, 1=STA, 2=AP |

首次启动检测 NVS → 如果有保存的 WiFi 信息 → 自动连接
没有配置 → 启动 AP 热点等待配网

---

## 四、连接流程

```
应用启动
    │
    ├─ NVS 有 WiFi 配置？
    │   ├─ YES → 连接 STA
    │   │           ├─ 成功 → 进入应用
    │   │           └─ 失败 → 启动 AP 配网
    │   │
    │   └─ NO  → 启动 AP 配网
    │               ├─ Web 页面 → 用户输入 SSID/PASS
    │               └─ 保存 NVS → 重启连接
    │
    LED: 蓝灯闪烁 = 连接中
         绿灯 0.5s = 成功
         红灯 2s   = 失败
```

---

## 五、模块结构

```
main/wifi/
├── wifi_manager.h        # WiFi 管理 API
├── wifi_manager.cpp      # STA + AP 模式管理
├── wifi_config_web.h     # Web 配网页面
└── wifi_config_web.cpp   # HTTP 服务器 + DNS 劫持
```

---

## 六、API 设计

```c
// wifi_manager.h

// 初始化 WiFi（从 NVS 加载配置）
bool wifi_init();

// 连接 STA（使用 NVS 中保存的配置）
bool wifi_connect_sta(uint32_t timeout_ms);

// 启动 AP 热点
bool wifi_start_ap(const char* ssid, const char* pass);

// 保存 WiFi 配置到 NVS
bool wifi_save_config(const char* ssid, const char* pass);

// 读取 WiFi 配置
bool wifi_load_config(char* ssid, size_t ssid_len, 
                      char* pass, size_t pass_len);

// 检查是否已连接
bool wifi_is_connected();

// 断开 WiFi
void wifi_disconnect();

// 获取 IP 地址
const char* wifi_get_ip();
```

---

## 七、LED 状态定义

| 状态 | LED | 说明 |
|------|-----|------|
| 未联网 | 蓝灯慢闪 | 等待配置或连接中 |
| 已连接 | 绿灯亮 0.5秒 | 连接成功 |
| 连接失败 | 红灯亮 2秒 | WiFi 不可用或密码错误 |
| 配网模式 | 蓝灯快闪 | AP 热点已开启，等待用户配置 |

---

## 八、实施路线

| 阶段 | 内容 | 预估 |
|------|------|------|
| P0 | NVS 存储 + STA 连接（从新闻模块提取通用代码） | 1天 |
| P1 | AP 热点 + Web 配网页 | 2天 |
| P2 | 断线自动重连 + 状态机 | 1天 |
