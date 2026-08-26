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
      _lastReconnectMs(0),
      _lastStatus("operational"),
      _lastPollTime(0),
      _lastSuccessMs(0),
      _hasData(false),
      _lastPushedStale(false) {}

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

    // WiFi.reconnect() tears down the association already in flight, so
    // calling it on every 100 ms poll restarts the handshake before it can
    // ever finish and the radio never associates. Retry slowly instead.
    uint32_t now = millis();
    if (_lastReconnectMs != 0 && now - _lastReconnectMs < WIFI_RECONNECT_INTERVAL_MS) {
        return;
    }
    _lastReconnectMs = now;

    ESP_LOGW(TAG, "Wi-Fi not connected. Attempting reconnect...");
    WiFi.reconnect();
}

void NetworkClient::checkStatusTransition(const String &providerId, const String &newStatus) {
    if (_lastStatus != newStatus) {
        ESP_LOGI(TAG, "Status transition for %s: %s -> %s",
                 providerId.c_str(), _lastStatus.c_str(), newStatus.c_str());

        if (newStatus == "degraded" || newStatus == "outage") {
            audio.triggerDegradationAlert();
        } else if (newStatus == "operational" && (_lastStatus == "degraded" || _lastStatus == "outage")) {
            audio.triggerRecoveryChime();
        }

        _lastStatus = newStatus;
    }
}

// Pushes the latest cached data to the UI, folding the device-observed
// connectivity age into the backend-reported "stale" flag so a run of
// failed polls reads as stale even if the payload itself claims otherwise.
void NetworkClient::pushToUI() {
    ProviderUIData local = _data;
    local.stale = _data.stale || (millis() - _lastSuccessMs >= STALE_AFTER_MS);
    _lastPushedStale = local.stale;

    if (ui.uiLock()) {
        ui.updateClaude(local);
        ui.uiUnlock();
    }
}

// Re-checks connectivity age independent of poll success/failure, so the
// stale badge turns on even when polls keep failing and no fresh payload
// ever arrives to trigger a redraw on its own.
void NetworkClient::refreshStaleness() {
    if (_hasData) {
        bool stale = _data.stale || (millis() - _lastSuccessMs >= STALE_AFTER_MS);
        if (stale != _lastPushedStale) {
            pushToUI();
        }
    }
}

void NetworkClient::processDashboardJSON(const String &payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        ESP_LOGE(TAG, "ArduinoJson deserialization failed: %s", error.c_str());
        return;
    }

    _lastSuccessMs = millis();

    JsonArray providers = doc["providers"];
    for (JsonObject p : providers) {
        String id = p["id"].as<String>();

        ProviderUIData uiData;
        uiData.id = id;
        uiData.name = p["name"].as<String>();
        uiData.status = p["status"].as<String>();
        uiData.authValid = p["auth_valid"].as<bool>();
        uiData.reLoginRequired = p["re_login_required"].as<bool>();
        uiData.authState = p["auth_state"].as<String>();
        uiData.stale = p["stale"].as<bool>();

        JsonObject q5h = p["metrics"]["quota_5h"];
        uiData.quota5hUsed = q5h["used"].as<float>();
        uiData.quota5hLimit = q5h["limit"].as<float>();
        uiData.quota5hPct = q5h["percentage"].as<float>();
        uiData.quota5hResetTime = q5h["reset_time"].as<String>();
        uiData.quota5hAvailable = q5h["available"].as<bool>();
        uiData.quota5hResetIn = q5h["reset_in_seconds"].as<int32_t>();

        JsonObject qWk = p["metrics"]["quota_weekly"];
        uiData.quotaWeeklyUsed = qWk["used"].as<float>();
        uiData.quotaWeeklyLimit = qWk["limit"].as<float>();
        uiData.quotaWeeklyPct = qWk["percentage"].as<float>();
        uiData.quotaWeeklyResetTime = qWk["reset_time"].as<String>();
        uiData.quotaWeeklyAvailable = qWk["available"].as<bool>();
        uiData.quotaWeeklyResetIn = qWk["reset_in_seconds"].as<int32_t>();

        if (id == "claude") {
            checkStatusTransition(id, uiData.status);
            _data = uiData;
            _hasData = true;
            pushToUI();
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
    refreshStaleness();
    uint32_t now = millis();
    if (now - _lastPollTime >= POLL_INTERVAL_MS || _lastPollTime == 0) {
        _lastPollTime = now;
        pollDashboard();
    }
}
