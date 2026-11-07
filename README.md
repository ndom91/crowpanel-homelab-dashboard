# CrowPanel homelab dashboard

ESPHome + LVGL firmware for the Elecrow CrowPanel ESP32-S3 5.79" e-paper
(792×272, black/white). It shows homelab status from Home Assistant: Proxmox
CPU and memory, GPU, service up/down tiles, public IPv4/IPv6, and connectivity.

```
Proxmox / service checks / network → Home Assistant entities
  → ESPHome native API → CrowPanel ESP32-S3 → LVGL e-paper UI
```

Display driver: [ESPboards/esphome-lvgl-crowpanel-epaper-5.79-4.2](https://github.com/ESPboards/esphome-lvgl-crowpanel-epaper-5.79-4.2),
pinned by commit in `packages/board.yaml`.

## Layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│ HOMELAB                                    ⌂ HA    ≋ -58 dBm   Sat 14:32 │
├────────────────────────┬────────────────────────┬────────────────────────┤
│ CPU                    │ MEMORY                 │ GPU                    │
│ 42%                    │ 61%                    │ --       no data       │
│ ████████░░░░░░░░       │ ████████████░░░░       │ ░░░░░░░░░░░░░░░░       │
├────────────────────────┴────────────────────────┴────────────────────────┤
│ [✓ plex  ] [✓ nas   ] [██✗ grafana██] [✓ pihole] [? unifi  ] [✓ hass  ]  │
│ [  UP    ] [  UP    ] [███ DOWN ████] [  UP    ] [ NO DATA ] [  UP    ]  │
├──────────────────────────────────────────────────────────────────────────┤
│ v4 203.0.113.42        v6 2001:db8::1                          upd 14:31 │
└──────────────────────────────────────────────────────────────────────────┘
```

- **UP:** thick outline + check. **DOWN:** fully inverted tile. **Unknown/unavailable:** thin outline + `?` / `NO DATA`.
- **Missing numbers** show `--` and an empty bar, never `0`.
- **At ≥ `alert_pct` (90%)**, the column caption inverts.
- **Banner over the header:**
  - `Waiting for Home Assistant...` until the first data arrives.
  - `OFFLINE since HH:MM - values are stale` after `offline_grace_s` (120 s) without Wi-Fi or an HA API connection. E-paper keeps the last image, so this is what stops old values from looking current.
- **Demo data** shows `HOMELAB - DEMO DATA` in the title.

## Repo layout

| File | Purpose |
|---|---|
| `packages/board.yaml` | ESP32-S3, PSRAM, GPIO7 panel power, display driver, buttons |
| `packages/base.yaml` | Logger, SNTP, Wi-Fi/API hooks. No secrets. |
| `packages/ui.yaml` | Fonts and LVGL layout (static styling only) |
| `packages/refresh.yaml` | Render script, refresh cadence, offline logic |
| `packages/data_demo.yaml` | Fake data, so the UI works with no entities |
| `packages/data_ha.yaml` | Home Assistant entities (IDs via substitutions) |
| `include/dashboard.h` | Render helpers: state → text/inversion |
| `ha-stub.yaml.example` | The device YAML for the HA ESPHome add-on |
| `dev.yaml` | Dev-only entry point: builds local `packages/` so unpushed changes can be tested |

## Deploying (HA ESPHome add-on)

The add-on holds a thin stub (`ha-stub.yaml.example`) with the device name,
Wi-Fi, API key, and your substitutions. It pulls the packages from this public
repo at build time. **Secrets, entity IDs, and LAN addresses live only in the
stub on HA. Never put them in this repo.**

1. Paste `ha-stub.yaml.example` into the device's YAML in the add-on. Keep the API key and AP password it generated.
2. Push changes to `main`.
3. In the add-on, click **Install → Wirelessly**.

`refresh: 0s` makes the add-on fetch the latest `main` on every build. Pin
`ref:` to a tag or SHA once things are stable.

**First flash only** (factory firmware) has to go over USB. In the add-on, use
**Install → Manual download → Factory format**, then flash it with
[web.esphome.io](https://web.esphome.io) in Chrome. After that, use OTA.

### Local checks

```sh
cp secrets.yaml.example secrets.yaml   # dummy values are fine for compiling
make config      # validate
make compile     # full build from local packages/ (dev.yaml)
make upload DEVICE=crowpanel.local   # OTA an unpushed build for testing
make logs DEVICE=crowpanel.local
```

ESPHome is pinned in `pyproject.toml` via uv. Keep it at the add-on's version.

## Entity mapping

Switch the stub's last package from `data_demo.yaml` to `data_ha.yaml`, then
set these substitutions in the stub:

| Substitution | Expected entity | Notes |
|---|---|---|
| `entity_cpu` | `sensor`, % (0-100) | Proxmox integration node CPU |
| `entity_mem` | `sensor`, % (0-100) | Proxmox gives used bytes/percent; use the percent entity |
| `entity_gpu` | `sensor`, % (0-100) | Needs its own source (Proxmox doesn't expose GPU). Unset shows `--`. |
| `entity_ip4` | any entity, state = address | e.g. a DNS IP sensor or router integration |
| `entity_ip6` | any entity, state = address | Check it's the address you want shown (prefix vs. host, temporary vs. stable). |
| `entity_svc1..6` | `binary_sensor` (or anything `on`/`off`) | Health check or ping per service |
| `svc1..6_name` | tile label | `""` hides the tile |
| `svc1..6_up_when` | `"on"` or `"off"` | `"off"` for `problem`-class sensors |

Notes:
- **Services are read as text, on purpose.** ESPHome's `homeassistant` binary sensor ignores `unavailable` and keeps its last `on`/`off`, so a dead check would still show UP. Text keeps the real state, which shows as `NO DATA`.
- **A ping only proves the host answers.** Prefer an HTTP check or an integration's own health entity for "is the app OK".
- **The device needs no HA-side config** beyond being adopted in the ESPHome integration. The HA sensors are `internal: true` on the device, so they are not re-exported.

## Refresh behaviour

- **Values batch to the minute.** Sensor callbacks only record that data arrived. The screen redraws on the minute, so the panel does one partial refresh (~0.6 s) per minute at most.
- **Immediate redraw** (2 s debounce) when a service changes state, or when Wi-Fi or the HA connection changes.
- **Full refresh** (clears ghosting, flashes the panel) every `full_refresh_every` (30) renders, at 03:00 nightly, and on a **long press of MENU (1-5 s)**.
- **Only changed widgets redraw.** The setters in `dashboard.h` skip unchanged text, because every LVGL invalidation costs an e-paper refresh.

## Changing the UI

- **Position, size, fonts:** `packages/ui.yaml`. All placement is absolute `x`/`y` on the 792×272 canvas. The comment block at the top lists the bands.
- **What a value shows / state rules:** `packages/refresh.yaml` (`render` script) and `include/dashboard.h`.
- **New icon:** add the MDI codepoint to the font's `extras.glyphs` in `ui.yaml`, and use it from `dashboard.h`.
- **Keep fonts at `bpp: 1`.** Use only pure black/white colors. Anything else dithers or smears.
- **New data point:**
  1. Add it to *both* `data_ha.yaml` and `data_demo.yaml` with the same `id`, calling `script.execute: on_data` in `on_value`.
  2. Add a widget to `ui.yaml`.
  3. Write it in `render`.

## Privacy

- **Logger defaults to `INFO`**, so entity states (including public IPs) don't reach the logs. Set `log_level: DEBUG` in the stub only while troubleshooting.
- **Public IPs are on the physical display.** Keep that in mind for photos.

## Hardware notes

- **GPIO7 powers the panel.** It must be high before the driver starts (handled in `board.yaml` `on_boot`).
- **PSRAM is `octal`** (N8R8 module). If you see PSRAM boot errors, try `quad`.
- **`invert_colors: true` is needed on the 5.79"**, or black and white swap.
- **GPIO45/46 strapping-pin warnings** at compile time are expected. Those pins are the panel's CS/DC.
