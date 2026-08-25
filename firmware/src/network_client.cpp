#include "network_client.h"
#include "ui_manager.h"
#include "audio_es8311.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_log.h"

static const char *TAG = "NetworkClient";

NetworkClient networkClient;

NetworkClient::NetworkClient()
    : _connected(false),
      _lastStatusClaude("operational"),
      _lastStatusAntigravity("operational"),
      _lastPollTime(0) {}

bool NetworkClient::init() {
    ESP_LOGI(TAG, "Initializing Wi-Fi client...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
    return true;
}

void NetworkClient::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!_connected) {
            _connected = true;
            ESP_LOGI(TAG, "Wi-Fi connected. IP: %s", WiFi.localIP().toString().c_str());
        }
        return;
    }

    _connected = false;
    ESP_LOGW(TAG, "Wi-Fi not connected. Attempting reconnect...");
    WiFi.reconnect();
}

void NetworkClient::checkStatusTransition(const String &providerId, const String &newStatus) {
    String *lastStatus = (providerId == "claude") ? &_lastStatusClaude : &_lastStatusAntigravity;

    if (*lastStatus != newStatus) {
        ESP_LOGI(TAG, "Status transition for %s: %s -> %s",
                 providerId.c_str(), lastStatus->c_str(), newStatus.c_str());

        if (newStatus == "degraded" || newStatus == "outage") {
            // Degradation alert
            audio.playDegradationAlert();
        } else if (newStatus == "operational" && (*lastStatus == "degraded" || *lastStatus == "outage")) {
            // Recovery chime
            audio.playRecoveryChime();
        }

        *lastStatus = newStatus;
    }
}

void NetworkClient::processDashboardJSON(const String &payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        ESP_LOGE(TAG, "ArduinoJson deserialization failed: %s", error.c_str());
        return;
    }

    JsonArray providers = doc["providers"];
    for (JsonObject p : providers) {
        String id = p["id"].as<String>();
        String name = p["name"].as<String>();
        String status = p["status"].as<String>();
        bool authValid = p["auth_valid"].as<bool>();
        bool reLoginReq = p["re_login_required"].as<bool>();

        JsonObject q5h = p["metrics"]["quota_5h"];
        float q5hUsed = q5h["used"].as<float>();
        float q5hLimit = q5h["limit"].as<float>();
        float q5hPct = q5h["percentage"].as<float>();
        String q5hReset = q5h["reset_time"].as<String>();

        JsonObject qWk = p["metrics"]["quota_weekly"];
        float qWkUsed = qWk["used"].as<float>();
        float qWkLimit = qWk["limit"].as<float>();
        float qWkPct = qWk["percentage"].as<float>();
        String qWkReset = qWk["reset_time"].as<String>();

        ProviderUIData uiData;
        uiData.id = id;
        uiData.name = name;
        uiData.status = status;
        uiData.authValid = authValid;
        uiData.reLoginRequired = reLoginReq;
        uiData.quota5hUsed = q5hUsed;
        uiData.quota5hLimit = q5hLimit;
        uiData.quota5hPct = q5hPct;
        uiData.quota5hResetTime = q5hReset;
        uiData.quotaWeeklyUsed = qWkUsed;
        uiData.quotaWeeklyLimit = qWkLimit;
        uiData.quotaWeeklyPct = qWkPct;
        uiData.quotaWeeklyResetTime = qWkReset;

        checkStatusTransition(id, status);

        if (id == "claude") {
            ui.updateClaude(uiData);
        } else if (id == "antigravity") {
            ui.updateAntigravity(uiData);
        }
    }
}

void NetworkClient::pollDashboard() {
    if (WiFi.status() != WL_CONNECTED) {
        ensureWiFi();
        return;
    }

    HTTPClient http;
    http.begin(DASHBOARD_API_URL);
    http.setTimeout(5000);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        processDashboardJSON(payload);
    } else {
        ESP_LOGW(TAG, "GET /api/dashboard failed with code: %d", httpCode);
    }

    http.end();
}

void NetworkClient::taskLoop() {
    ensureWiFi();
    uint32_t now = millis();
    if (now - _lastPollTime >= POLL_INTERVAL_MS || _lastPollTime == 0) {
        _lastPollTime = now;
        pollDashboard();
    }
}
