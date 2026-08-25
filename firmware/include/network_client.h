#pragma once

#include <Arduino.h>
#include "config.h"

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

    bool _connected;
    String _lastStatusClaude;
    String _lastStatusAntigravity;
    uint32_t _lastPollTime;
};

extern NetworkClient networkClient;
