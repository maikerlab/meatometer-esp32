#pragma once

/** Everything the app's mode state machine reacts to. */
enum class AppEventType {
    SnapshotReady,
    ButtonShort,
    ButtonLongReset,
    Touch,
    /** Status-bar Pair control (UI-6). Ignored while the backlight is off. */
    PairRequested,
    IdleTimeout,
    ConnectivityChanged,
};

struct AppEvent {
    AppEventType type;
};
