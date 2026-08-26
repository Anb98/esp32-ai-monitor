#pragma once

#include <Arduino.h>
#include "config.h"
#include "ui_manager.h"

class NetworkClient {
public:
    NetworkClient();
    bool init();
    void pollDashboard();
    void taskLoop();
    bool isConnected() const { return _connected; }

private:
    void ensureWiFi();
    void processDashboardJSON(const String &payload);
    void checkStatusTransition(const String &providerId, const String &newStatus);
    void refreshStaleness();
    void pushToUI();

    bool _connected;
    // Throttles reconnect attempts; see ensureWiFi().
    uint32_t _lastReconnectMs;
    String _lastStatus;
    uint32_t _lastPollTime;
    uint32_t _lastSuccessMs;
    bool _hasData;
    bool _lastPushedStale;
    ProviderUIData _data;
};

extern NetworkClient networkClient;
