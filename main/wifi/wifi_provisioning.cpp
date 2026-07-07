/**
 * PaperColor — WiFi Provisioning Server
 *
 * Provides:
 *  - DNS hijack (all domains → 192.168.4.1)
 *  - HTTP server with config page + REST APIs
 *  - AP auto-shutdown timer
 */

#include "wifi_provisioning.h"
#include "wifi_manager.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cJSON.h>
#include <M5Unified.hpp>

static const char* TAG = "WiFiProv";

// ── Constants ────────────────────────────────────────────────

#define DNS_PORT      53
#define HTTP_PORT     80
#define AP_TIMEOUT_MS (10 * 60 * 1000)  // 10 min idle timeout

// ── State ────────────────────────────────────────────────────

static httpd_handle_t       s_httpd       = NULL;
static TaskHandle_t         s_dns_task    = NULL;
static volatile bool        s_running     = false;
static uint32_t             s_last_activity = 0;

// ═══════════════════════════════════════════════════════════════
//  Config Page HTML (embedded)
// ═══════════════════════════════════════════════════════════════

static const char* CONFIG_HTML = R"HTMLDELIM(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PaperColor Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:-apple-system,sans-serif;background:#f5f5f5;padding:20px;max-width:480px;margin:auto}
  h1{font-size:22px;color:#333;margin:20px 0;text-align:center}
  .card{background:#fff;border-radius:12px;padding:20px;margin:16px 0;box-shadow:0 2px 8px rgba(0,0,0,0.08)}
  label{display:block;font-size:14px;color:#666;margin:12px 0 4px}
  input{width:100%;padding:12px;border:1px solid #ddd;border-radius:8px;font-size:16px}
  .eap-field{display:none}
  .toggle-wrap{display:flex;align-items:center;gap:12px;margin:12px 0;padding:12px;background:#f0f4ff;border-radius:8px}
  .toggle{position:relative;width:52px;height:28px;flex-shrink:0;cursor:pointer}
  .toggle input{opacity:0;width:0;height:0}
  .toggle .slider{position:absolute;inset:0;background:#ccc;border-radius:28px;transition:.3s}
  .toggle .slider::before{content:"";position:absolute;left:3px;top:3px;width:22px;height:22px;background:#fff;border-radius:50%;transition:.3s}
  .toggle input:checked+.slider{background:#007aff}
  .toggle input:checked+.slider::before{transform:translateX(24px)}
  .toggle-label{font-size:14px;font-weight:600;color:#333}
  .toggle-hint{font-size:12px;color:#999}
  .btn{width:100%;padding:14px;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;margin-top:16px}
  .btn-primary{background:#007aff;color:#fff}
  .btn-primary:disabled{opacity:0.5}
  .btn-secondary{background:#e8e8e8;color:#333;margin-top:8px}
  #status{margin-top:12px;padding:8px;border-radius:6px;font-size:14px;text-align:center}
  .ok{background:#d4edda;color:#155724}
  .err{background:#f8d7da;color:#721c24}
  .info{background:#d1ecf1;color:#0c5460}
  #scanList{margin-top:8px}
  .scan-item{padding:10px;border-bottom:1px solid #eee;cursor:pointer;display:flex;justify-content:space-between}
  .scan-item:hover{background:#f0f0f0}
  .sec{color:#999;font-size:12px}
</style>
</head>
<body>
<h1>📶 PaperColor</h1>
<div class="card">
  <label>WiFi Name (SSID)</label>
  <input id="ssid" placeholder="Select from list or type">
  <div class="toggle-wrap">
    <label class="toggle">
      <input type="checkbox" id="authToggle" onchange="toggleAuth()">
      <span class="slider"></span>
    </label>
    <div>
      <div class="toggle-label">802.1x 企业认证</div>
      <div class="toggle-hint">WPA2-Enterprise / PEAP-MSCHAPv2</div>
    </div>
  </div>
  <div id="eapFields" class="eap-field">
    <label>Username (EAP Identity)</label>
    <input id="username" placeholder="User@domain.com">
  </div>
  <label>Password</label>
  <input id="pass" type="password" placeholder="WiFi password / EAP password">
  <button class="btn btn-secondary" onclick="scan()">🔍 Scan Networks</button>
  <div id="scanList"></div>
  <button class="btn btn-primary" onclick="connect()" id="connectBtn">Connect</button>
  <div id="status"></div>
</div>
<div class="card" style="font-size:13px;color:#999">
  Hotspot: <span id="apName"></span><br>
  仅支持 2.4GHz 网络
</div>

<script>
async function scan() {
  document.getElementById('scanList').innerHTML = '<div class="info" style="padding:8px">Scanning...</div>';
  try {
    const r = await fetch('/api/scan');
    const nets = await r.json();
    let html = '';
    nets.forEach(n => {
      const sec = n.secure ? 'Y' : 'N';
      html += `<div class="scan-item" onclick="pick('${n.ssid}')"><span>${n.ssid}</span><span class="sec">${sec} ${n.rssi}dBm</span></div>`;
    });
    document.getElementById('scanList').innerHTML = html;
  } catch(e) {
    document.getElementById('scanList').innerHTML = '<div class="err" style="padding:8px">Scan failed</div>';
  }
}
function pick(s) { document.getElementById('ssid').value = s; }
function toggleAuth() {
  const on = document.getElementById('authToggle').checked;
  document.getElementById('eapFields').style.display = on ? 'block' : 'none';
  document.getElementById('pass').placeholder = on ? 'EAP password' : 'WiFi password (leave blank if open)';
}
async function connect() {
  const ssid = document.getElementById('ssid').value.trim();
  if (!ssid) { show('Please enter SSID', 'err'); return; }
  const auth = document.getElementById('authToggle').checked ? 'enterprise' : 'psk';
  const username = document.getElementById('username').value.trim();
  const pass = document.getElementById('pass').value;
  if (auth === 'enterprise' && !username) {
    show('Username required for Enterprise', 'err'); return;
  }
  const body = auth === 'enterprise'
    ? JSON.stringify({ssid, auth, identity: username, username, pass})
    : JSON.stringify({ssid, pass});
  document.getElementById('connectBtn').disabled = true;
  show('Saving...', 'info');
  try {
    const r = await fetch('/api/config', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body
    });
    const j = await r.json();
    if (j.status === 'ok') {
      show('Saved successfully', 'ok');
    } else {
      show(j.message || 'Failed', 'err');
      document.getElementById('connectBtn').disabled = false;
    }
  } catch(e) {
    show('Connection error', 'err');
    document.getElementById('connectBtn').disabled = false;
  }
}
function show(msg, cls) {
  const s = document.getElementById('status');
  s.className = cls; s.textContent = msg;
}
document.getElementById('apName').textContent = location.hostname;
</script>
</body>
</html>
)HTMLDELIM";

// ═══════════════════════════════════════════════════════════════
//  DNS Hijack Server
// ═══════════════════════════════════════════════════════════════

static void dns_task_func(void*)
{
    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    uint32_t last_tick_check = 0;
    while (s_running) {
        // Non-blocking check with 1s timeout for periodic tick
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv = {1, 0};  // 1s select timeout
        int sel = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (sel == 0) {
            // Timeout — periodic AP idle check (every 5s)
            uint32_t now = esp_timer_get_time() / 1000;
            if (now - last_tick_check > 5000) {
                last_tick_check = now;
                if (now - s_last_activity > AP_TIMEOUT_MS) {
                    ESP_LOGI(TAG, "AP idle timeout, shutting down");
                    s_running = false;
                    break;
                }
            }
            continue;
        }
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
        if (n < 12) continue;

        // Respond to ANY query with 192.168.4.1
        uint16_t id = (buf[0] << 8) | buf[1];
        buf[0] = id >> 8; buf[1] = id & 0xFF;

        // Flags: response + authoritative + recursion desired
        buf[2] = 0x85; buf[3] = 0x80;
        // QDCOUNT = 1, ANCOUNT = 1
        buf[6] = 0; buf[7] = 1;  // ANCOUNT
        // Answer at offset n
        int len = n;
        buf[len++] = 0xC0; buf[len++] = 0x0C;  // pointer to query name
        buf[len++] = 0x00; buf[len++] = 0x01;  // A record
        buf[len++] = 0x00; buf[len++] = 0x01;  // class IN
        buf[len++] = 0x00; buf[len++] = 0x00;  // TTL
        buf[len++] = 0x00; buf[len++] = 0x3C;  // 60s
        buf[len++] = 0x00; buf[len++] = 0x04;  // data length
        buf[len++] = 192; buf[len++] = 168;     // 192.168.4.1
        buf[len++] = 4;   buf[len++] = 1;

        sendto(sock, buf, len, 0, (struct sockaddr*)&from, fromlen);
    }

    close(sock);
    // AP timeout expired — stop AP and provisioning
    wifi_prov_stop();
    wifi_mgr_stop_ap();
    vTaskDelete(NULL);
}

// ═══════════════════════════════════════════════════════════════
//  HTTP Handlers
// ═══════════════════════════════════════════════════════════════

static esp_err_t handle_get_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML));
    s_last_activity = esp_timer_get_time() / 1000;
    return ESP_OK;
}

static esp_err_t handle_post_config(httpd_req_t* req)
{
    s_last_activity = esp_timer_get_time() / 1000;

    char buf[256] = {};
    int len = req->content_len;
    if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
    httpd_req_recv(req, buf, len);
    buf[len] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!json) {
        const char* err = R"({"status":"err","message":"Invalid JSON"})";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err, strlen(err));
        return ESP_OK;
    }

    cJSON* j_ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON* j_auth = cJSON_GetObjectItem(json, "auth");
    cJSON* j_pass = cJSON_GetObjectItem(json, "pass");

    if (!j_ssid || !cJSON_IsString(j_ssid)) {
        cJSON_Delete(json);
        const char* err = R"({"status":"err","message":"SSID required"})";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err, strlen(err));
        return ESP_OK;
    }

    const char* ssid = j_ssid->valuestring;
    const char* auth = (j_auth && cJSON_IsString(j_auth)) ? j_auth->valuestring : WIFI_AUTH_TYPE_PSK;
    const char* pass = (j_pass && cJSON_IsString(j_pass)) ? j_pass->valuestring : "";

    if (strcmp(auth, WIFI_AUTH_TYPE_ENTERPRISE) == 0) {
        cJSON* j_id = cJSON_GetObjectItem(json, "identity");
        cJSON* j_un = cJSON_GetObjectItem(json, "username");
        const char* identity = (j_id && cJSON_IsString(j_id)) ? j_id->valuestring : "";
        const char* username = (j_un && cJSON_IsString(j_un)) ? j_un->valuestring : identity;
        ESP_LOGI(TAG, "Config received: %s (Enterprise, id=%s)", ssid, identity);
        wifi_mgr_save_network_ext(0, ssid, WIFI_AUTH_TYPE_ENTERPRISE, identity, username, pass);
    } else {
        ESP_LOGI(TAG, "Config received: %s (PSK)", ssid);
        wifi_mgr_save_network(0, ssid, pass);
    }

    cJSON_Delete(json);

    const char* ok = R"({"status":"ok","message":"Saved successfully"})";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, ok, strlen(ok));

    // Return from handler first, then connect in a deferred task
    ESP_LOGI(TAG, "Provisioning done, connecting...");
    wifi_mgr_stop_ap();

    // One-shot deferred connect (non-blocking for HTTP handler)
    xTaskCreate([](void*) { wifi_mgr_connect_sta(30000); vTaskDelete(NULL); },
                "prov_conn", 4096, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t handle_get_scan(httpd_req_t* req)
{
    s_last_activity = esp_timer_get_time() / 1000;

    uint16_t count = 0;
    wifi_ap_record_t records[16];

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_scan_start(NULL, true);
    esp_wifi_scan_get_ap_records(&count, records);

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < count && i < 16; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char*)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(item, "secure",
            records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, item);
    }

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);

    esp_wifi_set_mode(WIFI_MODE_AP);
    return ESP_OK;
}

static esp_err_t handle_get_status(httpd_req_t* req)
{
    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "connected",
        wifi_mgr_get_state() == WIFI_STATE_STA_OK);
    cJSON_AddStringToObject(j, "ip", wifi_mgr_get_ip());
    cJSON_AddStringToObject(j, "ap", wifi_mgr_get_ap_ssid());

    char* json = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════

void wifi_prov_start(void)
{
    if (s_running) return;
    s_running = true;
    s_last_activity = esp_timer_get_time() / 1000;

    // ── DNS task ──
    xTaskCreate(dns_task_func, "dns", 3072, NULL, 5, &s_dns_task);

    // ── HTTP server ──
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port    = HTTP_PORT;
    cfg.max_open_sockets = 5;

    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        httpd_uri_t h_root   = {"/",       HTTP_GET, handle_get_root};
        httpd_uri_t h_config = {"/api/config", HTTP_POST, handle_post_config};
        httpd_uri_t h_scan   = {"/api/scan",   HTTP_GET, handle_get_scan};
        httpd_uri_t h_status = {"/api/status", HTTP_GET, handle_get_status};
        httpd_uri_t h_any    = {"/*",      HTTP_GET, handle_get_root};
        httpd_register_uri_handler(s_httpd, &h_root);
        httpd_register_uri_handler(s_httpd, &h_config);
        httpd_register_uri_handler(s_httpd, &h_scan);
        httpd_register_uri_handler(s_httpd, &h_status);
        httpd_register_uri_handler(s_httpd, &h_any);

        ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_PORT);
    } else {
        ESP_LOGE(TAG, "HTTP server start failed");
    }
}

void wifi_prov_stop(void)
{
    s_running = false;
    if (s_dns_task) { vTaskDelete(s_dns_task); s_dns_task = NULL; }
    if (s_httpd)    { httpd_stop(s_httpd); s_httpd = NULL; }
}

void wifi_prov_tick(void)
{
    // Auto-close AP if idle too long (no client activity)
    uint32_t now = esp_timer_get_time() / 1000;
    if (now - s_last_activity > AP_TIMEOUT_MS) {
        ESP_LOGI(TAG, "AP idle timeout");
        wifi_prov_stop();
        wifi_mgr_stop_ap();
    }
}
