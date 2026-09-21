#pragma once

/** Everything the app's mode state machine reacts to. */
enum class AppEventType {
    SnapshotReady,
    ButtonShort,
    ButtonLongReset,
    Touch,
    IdleTimeout,
    ConnectivityChanged,
};

struct AppEvent {
    AppEventType type;
};
