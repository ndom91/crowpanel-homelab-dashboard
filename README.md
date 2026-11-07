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
- **Demo data** shows an inverted `DEMO DATA - NOT LIVE` title, and placeholder addresses (`x.x.x.x (demo)`).

## Deploying (HA ESPHome add-on)

The add-on holds a thin stub (`ha-stub.yaml.example`) with the device name,
Wi-Fi, API key, and your substitutions. It pulls the packages from this public
repo at build time. **Secrets, entity IDs, and LAN addresses live only in the
stub on HA. Never put them in this repo.**

1. Paste `ha-stub.yaml.example` into the device's YAML in the add-on. Keep the API key and AP password it generated.
2. Push changes to `main`.
3. In the add-on, click **Install → Wirelessly**.
4. **Add the device to HA's ESPHome integration** (Settings → Devices & services → Discovered → crowpanel, or Add integration → ESPHome). The Builder add-on alone does *not* do this. Without it, no entity states reach the device and the panel stays on `Waiting for Home Assistant...`.

`refresh: 0s` makes the add-on fetch the latest `main` on every build. Pin
`ref:` to a tag or SHA once things are stable.

**First flash only** (factory firmware) has to go over USB. In the add-on, use
**Install → Manual download → Factory format**, then flash it with
[web.esphome.io](https://web.esphome.io) in Chrome. After that, use OTA.

### Development

Local builds, testing, and how the code fits together: see
[`AGENTS.md`](AGENTS.md).

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
- **An unavailable service shows `NO DATA`,** never its last known state.
- **Proxmox API token needs its own ACL.** With "Privilege Separation" on, a token ignores its user's and group's roles. Grant it read-only access, or the node shows up with zero entities:
  `pveum acl modify / --tokens '<user>@pve!<token>' --roles PVEAuditor`
- **Proxmox `*_status` binary sensors mean "running", not "healthy".** A container whose app crashed still shows UP.
- **A ping only proves the host answers.** Prefer an HTTP check or an integration's own health entity for "is the app OK".
- **No other HA-side config is needed** beyond adding the device to the ESPHome integration (deploy step 4).

## Refresh behaviour

- **Values batch to the minute.** Sensor callbacks only record that data arrived. The screen redraws on the minute, so the panel does one partial refresh (~0.6 s) per minute at most.
- **Immediate redraw** (2 s debounce) when a service changes state, or when Wi-Fi or the HA connection changes.
- **Full refresh** (clears ghosting, flashes the panel) every `full_refresh_every` (30) renders, at 03:00 nightly, and on a **long press of MENU (1-5 s)**.

## Changing the UI

- **Tile names, entities, thresholds:** substitutions in your add-on YAML (`svcN_name`, `alert_pct`, `full_refresh_every`, `offline_grace_s`, ...). Edit them in the add-on, Save, then Install. No push needed.
- **Layout, fonts, new widgets:** edit `packages/ui.yaml` / `packages/refresh.yaml`, push to `main`, then Install. See `AGENTS.md` for how rendering works.

## Privacy

- **Logger defaults to `INFO`**, so entity states (including public IPs) don't reach the logs. Set `log_level: DEBUG` in the stub only while troubleshooting.
- **Public IPs are on the physical display.** Keep that in mind for photos.

## Hardware notes

- **Pins, PSRAM, and driver settings** are in `packages/board.yaml` and `AGENTS.md`.
- **Expected compile warnings:** strapping-pin warnings for GPIO45/46 (the panel's CS/DC).
