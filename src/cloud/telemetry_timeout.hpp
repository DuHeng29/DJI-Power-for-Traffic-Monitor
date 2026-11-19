#pragma once

#include <chrono>

namespace dji_power::cloud {
inline constexpr auto kTelemetryTimeout = std::chrono::minutes(3);

enum class TelemetryTimeoutAction { none, wait_for_telemetry, reconnect };

constexpr TelemetryTimeoutAction OnTelemetryIdle(std::chrono::steady_clock::duration idle,
                                                bool subscribed, bool auto_reconnect) {
    if (!subscribed || idle < kTelemetryTimeout) return TelemetryTimeoutAction::none;
    return auto_reconnect ? TelemetryTimeoutAction::reconnect : TelemetryTimeoutAction::wait_for_telemetry;
}
} // namespace dji_power::cloud
