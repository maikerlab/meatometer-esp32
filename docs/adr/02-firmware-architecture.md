# ADR: Firmware Architecture (layers + event-driven flow)

**Status:** Accepted
**Date:** 2026-09-21

## Context

The firmware started as a copy of the ESP-Matter *light* example: `Hal` exposed hue/saturation/XY setters, `app_driver.cpp` mapped Matter colour clusters onto an LED, and `app_main.cpp` ran a single 5-sample / 5 s temperature loop into one endpoint.

[REQUIREMENTS.md](../REQUIREMENTS.md) needs something else: three probes sampled at 1 Hz (PR-6), a touchscreen UI (UI-1, UI-2), an LED state policy (UI-4), a button with two meanings (UI-3), persisted probe names (PR-7, PR-8), and Matter as an *addition* that local measurement must never depend on (NF-2). [ADR 01](01-hardware-selection.md) also leaves the chamber-probe hardware in flux: MAX6675 today, M5Stack Kmeter Unit once the parts arrive.

Two decisions are needed: how the code is layered, and how it executes.

## Decision

### 1. Layers

Five modules plus a coordinator. Abstract base classes exist only where a second implementation is actually planned.

```
App  ── coordinator: tasks, mode state machine, wiring
 ├── Hmi              ── display content, touch, button input, LED output
 ├── ProbeManager     ── owns TemperatureProbe instances, 1 Hz sampling
 ├── SettingsStore    ── NVS: probe names, factory reset
 ├── Connectivity     ── Matter (v2) or nothing (v1)
 └── Hal              ── board I/O: LED, display power, battery, I2C bus
```

- **Hal** touches silicon and board resources only. No Celsius conversion, no screens, no Matter.
- **Probes** own presence detection and temperature conversion.
- **Hmi** owns what is on screen and what an input *is*; it does not own device state.
- **Settings** owns NVS.
- **Connectivity** owns Matter endpoints, CN-9 clamping, and commissioning state.

`ProbeManager` never includes Matter headers. That makes NF-2 structural rather than a promise.

**Abstractions kept:** `TemperatureProbe` (MAX6675 now, Kmeter/NTC later) and `Connectivity` (none in v1, Matter in v2).

**Operation results** use one firmware-wide `Result` (`Ok` / `Failed` / `InvalidArgument` / `NotReady`). Per-layer status enums would carry the same four codes and force conversions at every boundary (HMI checking HAL, `ProbeManager` checking a probe). `Result` is not HAL-specific.

**Abstractions deliberately not created:** `IDisplay`, `ILed`, `IButton`, `IBattery` (one board, no second implementation planned), an event-bus or observer framework (one producer, one consumer), per-probe tasks, and strategy objects for Steinhart-Hart (coefficients are plain member data).

### 2. Probe abstraction

```cpp
class TemperatureProbe {
public:
    ProbeId id() const;
    ProbeKind kind() const;
    virtual uint32_t conversion_time_ms() const;   // default 0
    virtual Result init();                      // default no-op
    virtual Result start_conversion();          // default no-op
    virtual Result read(float &celsius, bool &connected) = 0;
};
```

`Max6675Probe` wraps the `hayschan/max6675` component that is already a dependency. `KmeterProbe`, `NtcProbe` and `Ads1115` slot in later without touching `ProbeManager`, the HMI, or Matter — only the wiring in `App` changes. `SimulatedProbe` stands in for the food probes until that hardware is chosen, and keeps the whole pipeline testable without a grill.

The `hayschan/max6675` component exposes a global `MAX6675_init()` / `readCelsius()` C API, so only one `Max6675Probe` instance can exist. The class enforces that and hides it; the restriction disappears with `KmeterProbe`.

### 3. Execution model: two tasks, not a super-loop

A super-loop forces the slowest operation into everyone's latency budget — a 220 ms MAX6675 conversion and a full-screen redraw would sit in the same 1 Hz path. A state machine over the entire application is more machinery than three probes and one button justify. So: **two tasks plus a small mode state machine inside `App`.**

```
Sampler task (prio 3, 1 Hz, vTaskDelayUntil)
  start_conversion() on all probes
  one vTaskDelay of max(conversion_time_ms())
  read() all probes  ->  DeviceSnapshot
  |                                   |
  | xQueueOverwrite (mailbox, len 1)  | SystemLayer().ScheduleLambda
  v                                   v
App/UI task (prio 2, 50 ms tick)    CHIP event loop
  drain event queue                   SetMeasuredValue (clamped, CN-9)
  mode state machine
  Hmi render + StatusLed
```

- The **sampler task** is the sole owner of the probe drivers and the sensor bus, so no bus mutex is needed. It is the one place blocking I/O is allowed.
- The **app/UI task** reads the newest snapshot from a length-1 `xQueueOverwrite` mailbox. A slow redraw drops stale frames instead of backing up the sampler.
- **Matter gets no task of ours.** Publishing goes through `chip::DeviceLayer::SystemLayer().ScheduleLambda(...)`, which returns immediately and runs the attribute write on the CHIP event loop, where it has to happen anyway.

Mode state machine in `App`: `Boot`, `Running`, `DisplayOff`, `Commissioning` (v2), `FactoryReset`.
Events: `SnapshotReady`, `ButtonShort`, `ButtonLongReset`, `Touch`, `IdleTimeout`, `ConnectivityChanged`.
An enum plus one `handle()` switch — enough to make UI-3, UI-4, UI-5 and UI-9 explicit instead of scattering booleans.

### 4. Rules for avoiding long blocking calls

- **Two-phase probe reads.** Conversions overlap across probes, so MAX6675's 220 ms is paid once per cycle instead of once per probe. This still holds at the 6 probes of NF-4.
- **The sampler never blocks on a consumer.** `xQueueOverwrite` for the snapshot; every event-queue post uses timeout 0.
- **Bus errors are cheap.** A failed read marks that probe disconnected (PR-5); it does not stall the cycle.
- **No NVS writes in the sampler.** `SettingsStore` keeps names in RAM behind a short mutex; writes happen in the app task.
- **Long press by timer, not polling.** `iot_button` reports `BUTTON_LONG_PRESS_START` from its own timer at the configured 10 s (UI-3); the callback posts an event and returns.
- **Factory reset is deferred** to the app task, never performed inside a button or Matter callback.
- **Redraw only on change**, since the data rate is 1 Hz and a 320x240 repaint is not free.
- **Never call blocking Matter APIs from the UI task**, and never take the CHIP stack lock while holding a UI lock.

The sampler logs a warning if a cycle exceeds its budget, so a slow probe cannot silently break PR-6.

## Consequences

- Swapping MAX6675 for the Kmeter Unit is a new `TemperatureProbe` subclass plus one line of wiring in `App`.
- v1 runs with `NullConnectivity` and no network at all; `MatterConnectivity` is selected by `CONFIG_MEATOMETER_ENABLE_MATTER` and creates one `TemperatureMeasurement` endpoint per probe (CN-3), clamped at 327.67 °C when reporting (CN-9).
- The display is not wrapped in the HAL. `Hmi` renders into text lines today and will call M5GFX/LVGL directly once CoreS3 hardware is in hand; HAL only owns backlight power.
- Two tasks and two queues cost roughly 8 KB of stack and ~300 bytes of queue memory — acceptable on an ESP32-S3 with PSRAM.
- Adding a probe past 6 (NF-4) still needs the staggered-polling redesign called out in ADR 01; the two-phase interface is what makes that a `ProbeManager` change and nothing else.

## Layout

```
main/
  app.{h,cpp}                  coordinator, tasks, mode state machine
  app_main.cpp                 App::run() only
  types.h  esp_status.h        shared value types, esp_err_t <-> Result
  hal/hal.h  hal/m5stack_hal.{h,cpp}
  probes/temperature_probe.h
  probes/max6675_probe.{h,cpp}  probes/simulated_probe.{h,cpp}
  probes/probe_manager.{h,cpp}
  hmi/hmi.{h,cpp}  hmi/main_screen.{h,cpp}  hmi/status_led.{h,cpp}
  settings/settings_store.{h,cpp}
  connectivity/connectivity.h
  connectivity/null_connectivity.{h,cpp}  connectivity/matter_connectivity.{h,cpp}
```

## Open follow-ups

- `Hal::i2c_select()` and `read_battery()` are stubs until CoreS3 SE + PaHub + AXP2101 are wired.
- Touch input is stubbed in `Hmi::poll_input()`; UI-9's idle timeout is implemented but only the button can currently wake the display.
- Probe names are readable/writable locally but not yet over Matter (CN-8).
