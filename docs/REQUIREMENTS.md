# Smart Multi-Probe Grill Thermometer — Requirements

## 1. Purpose & Scope

Smart multi-probe grill thermometer that measures cooking-chamber and food core temperatures and displays them locally.

- **v1** — a standalone functioning prototype: continuous local measurement and display, LED, button. No network functionality.
- **v2** — adds Matter (Wi-Fi) integration, probe renaming, battery reporting, display power management, and target-temperature alarms.

Priority values used below: **v1** = required for a functioning prototype. **v2** = nice-to-have and usability features, built on top of v1.

## 2. Probes & Measurement

| ID   | Requirement                                                                                                                                                                                                                                                               | Priority |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| PR-1 | The device provides 3 probe ports: 1× chamber probe (Type-K thermocouple via an I2C thermocouple-to-digital module, e.g. M5Stack Kmeter Unit) and 2× food core probes (NTC thermistor, ThermoWorks TX-1001X-OP or equivalent, via an I2C ADC module, e.g. Grove ADS1115). | v1       |
| PR-2 | Chamber probe: typical operating range 20–800 °C, accuracy ±5 °C. No software-enforced min/max clamp.                                                                                                                                                                     | v1       |
| PR-3 | Food probes: typical operating range 20–100 °C, accuracy ±1 °C. Values are linearized using the probe's Steinhart-Hart coefficients. No software-enforced min/max clamp.                                                                                                  | v1       |
| PR-4 | All probes connect via pluggable I2C/Grove cabling to off-the-shelf sensor modules. No soldering, no opening the enclosure.                                                                                                                                               | v1       |
| PR-5 | The device automatically detects whether a probe is present on each port and marks it connected/disconnected.                                                                                                                                                             | v1       |
| PR-6 | While powered on, all connected probes are sampled once per second, continuously, regardless of display or network state.                                                                                                                                                 | v1       |
| PR-7 | Each probe port has an associated name, defaulting to "Environment", "Meat 1", "Meat 2".                                                                                                                                                                                  | v1       |
| PR-8 | Probe names are editable and persist across power cycles; factory reset restores the defaults.                                                                                                                                                                            | v2       |
| PR-9 | The probe interface is extensible: additional probes can be added via an I2C hub (e.g. Unit PaHub) without hardware redesign, up to a documented maximum, without breaking the 1 Hz sampling requirement (PR-6).                                                          | v2       |

## 3. Connectivity (Matter)

| ID   | Requirement                                                                                                                                                                                                                | Priority |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| CN-1 | The device is a Matter end device, commissioned via BLE.                                                                                                                                                                   | v2       |
| CN-2 | Operational transport is Wi-Fi only.                                                                                                                                                                                       | v2       |
| CN-3 | Each probe is exposed as its own endpoint with a standard TemperatureMeasurement cluster.                                                                                                                                  | v2       |
| CN-4 | Only the `MeasuredValue` attribute is reported; `MinMeasuredValue`/`MaxMeasuredValue` are not implemented.                                                                                                                 | v2       |
| CN-5 | Values are reported at the 1 Hz sample rate (no throttling).                                                                                                                                                               | v2       |
| CN-6 | Network and Matter credentials persist across power cycles; connection and reconnection behaviour follows the Matter SDK's standard handling.                                                                              | v2       |
| CN-7 | Battery level is exposed via the Matter PowerSource cluster.                                                                                                                                                               | v2       |
| CN-8 | Probe names are readable and writable over Matter.                                                                                                                                                                         | v2       |
| CN-9 | Values exceeding the TemperatureMeasurement cluster's representable range are clamped to the maximum supported value (327.67 °C) when reported over Matter. The display always shows the actual measured value, unclamped. | v2       |

## 4. User Interface

| ID    | Requirement                                                                                                                                                                                            | Priority                            |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------- |
| UI-1  | The device has a built-in small color touchscreen.                                                                                                                                                     | v1                                  |
| UI-2  | The main screen shows the name and current temperature of every connected probe; disconnected probes are not listed.                                                                                   | v1                                  |
| UI-3  | One button: short press toggles the display on/off; press ≥ 10 s triggers a factory reset.                                                                                                             | v1                                  |
| UI-4  | One addressable RGB LED (e.g. WS2812) indicates device state, in priority order: blue = commissioning mode active, green = connected to Matter, yellow = not connected.                                | v1 (yellow only) / v2 (blue, green) |
| UI-5  | Factory reset restores all persisted settings to defaults (v1: probe names and display settings; v2: additionally erases Matter and network credentials and returns the device to commissioning mode). | v1 / v2                             |
| UI-6  | The BLE commissioning payload/QR code is shown on the display while in commissioning mode. No other connectivity status is shown on-screen.                                                            | v2                                  |
| UI-7  | Probe names can be edited via the touchscreen.                                                                                                                                                         | v2                                  |
| UI-8  | Battery level is shown on the display.                                                                                                                                                                 | v2                                  |
| UI-9  | The display turns off after 5 minutes without touch interaction; a touch wakes it.                                                                                                                     | v2                                  |
| UI-10 | The touchscreen supports setting and confirming a target-temperature alarm per probe.                                                                                                                  | v2                                  |

## 5. Power

| ID   | Requirement                                                                                                                                                 | Priority |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| PW-1 | The device runs from an internal rechargeable battery, from USB, or both.                                                                                   | v1       |
| PW-2 | The device is operable with no battery installed, powered only via USB.                                                                                     | v1       |
| PW-3 | Connecting USB charges the battery (when present) while the device remains operational.                                                                     | v1       |
| PW-4 | At low battery, the device stays operational as long as sufficient power is available; the user can monitor the level (UI-8, CN-7) and act before shutdown. | v2       |

## 6. Non-functional

| ID   | Requirement                                                                                                                                                                                                                                      | Priority |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- |
| NF-1 | Small, compact form factor; dimensions to be finalized once hardware is selected.                                                                                                                                                                | v1       |
| NF-2 | Local measurement never depends on cloud or network availability.                                                                                                                                                                                | v1       |
| NF-3 | Easy to use and assemble: the project must be buildable by a third party who buys the listed off-the-shelf hardware, clones the repository, and flashes the firmware — without soldering, custom PCB fabrication, or 3D-printed enclosure parts. | v1       |
| NF-4 | Documented maximum extensibility: up to 6 probes can be added using a single I2C hub with true parallel 1 Hz sampling and no firmware redesign; higher counts are possible but require a concurrent/staggered polling redesign (see ADR).        | v2       |

## 7. Open Points

- Final enclosure dimensions and weight — pending hardware selection.
- Confirm the chosen battery accessory (M5GO Battery Bottom3) is validated for the CoreS3 SE variant specifically, not just the full CoreS3.