# AGENTS.md

Working notes for agents (and humans) changing this repo. User-facing setup is
in `README.md`; this file covers how the code fits together, how to build and
test it, and the traps already hit.

## What this is

ESPHome + LVGL firmware for the Elecrow CrowPanel ESP32-S3 5.79" e-paper
(792×272, 1-bit black/white, dual SSD1683, WROOM-1 N8R8). It renders a homelab
status page from Home Assistant entities received over the ESPHome native API.
Home Assistant is the only data source. Don't add direct connections from the
device to individual services.

This repo is **public**. Secrets, entity IDs, hostnames, LAN IPs, and the
user's public IPs must never be committed (see "Secrets policy").

## Repo map

| Path | Role |
|---|---|
| `packages/board.yaml` | ESP32-S3, PSRAM (octal), GPIO7 panel power, `crowpanel_epaper` driver (pinned SHA), front-panel buttons |
| `packages/base.yaml` | Logger level, SNTP (`ntp_server` substitution), Wi-Fi/API connect hooks. **No wifi/api/ota blocks.** |
| `packages/ui.yaml` | Fonts and the LVGL page ("1b" design: header bar, 3 canvas charts, 6 service cells, footer). Static styling only. Band map in its header comment. |
| `packages/refresh.yaml` | Globals, `on_data` / `render_now` / `render` scripts, minute tick, full-refresh cadence, MENU long-press |
| `packages/data_ha.yaml` | `homeassistant` sensors. Entity IDs are substitutions with `TODO` defaults. |
| `packages/data_demo.yaml` | Template sensors with fake data, and sets `demo_mode: "true"` |
| `include/dashboard.h` | C++ render helpers: setters, chart history + canvas drawing, ISO-8601 parsing, durations, service state |
| `ha-stub.yaml.example` | Template for the device YAML in the HA ESPHome add-on (git-based `packages:`) |
| `dev.yaml` | Dev-only entry: same as the stub but `!include`s local `packages/` |
| `Makefile` | `config`, `compile`, `upload`, `logs`, `check-secrets` |
| `pyproject.toml` / `uv.lock` | ESPHome pinned via uv (keep at the add-on's version, currently 2026.10.2) |

Gitignored and local only: `secrets.yaml`, `ha-stub.yaml` (the real stub with
the user's API key, entity IDs, and LAN settings), `ha-stub.local.yaml` (the
same, but `!include`ing local packages, for testing unpushed changes with real
data), `.esphome/`.

## Architecture

### Two entry points, one set of packages

- **Production:** the HA ESPHome Builder add-on (slug `5c53de3b_esphome`) holds the device YAML. That YAML is a copy of `ha-stub.yaml.example` with real values. It carries `wifi`, `api` (encryption key), `ota`, `captive_portal`, the `domain` and NTP settings, and every entity/name substitution. Its `packages:` block pulls `packages/*.yaml` from GitHub `main` with `refresh: 0s`. **Pushing to `main` changes what the add-on builds next.**
- **Dev:** `dev.yaml` holds the same blocks with `!secret`s from a local `secrets.yaml`, and `!include`s local packages, so unpushed changes can be compiled and OTA'd.
- **Package hooks merge into the top-level blocks.** For example, `base.yaml` adds `on_connect` / `on_client_connected` to the stub's `wifi` / `api`.
- **Top-level substitutions override package defaults.** That's how the stub injects entity IDs and names.

### Data contract

`ui.yaml` and `refresh.yaml` only read these IDs. Both data packages must
provide all of them with the same types:

- `sensor`: `d_cpu`, `d_mem`, `d_gpu`. Percent. NaN means unknown/unavailable.
- `text_sensor`: `d_svc1` .. `d_svc6`. The raw HA state string.
- `text_sensor`: `d_ip4`, `d_ip6`. The raw HA state string.
- `text_sensor`: `d_svc1_up` .. `d_svc6_up`, `d_uptime`. Boot time as ISO-8601 (HA `uptime` device_class). The device computes durations from it with `parse_iso8601`.

Each data source's `on_value` calls `script.execute: on_data`. Services call
`on_service_data` instead, which also triggers a debounced redraw.

**Services are deliberately `text_sensor`, not `binary_sensor`.** ESPHome's
`homeassistant` binary sensor logs a warning on `unavailable` and keeps its last
on/off. A dead check would keep showing UP. `parse_service()` maps
`up_when` → UP, the opposite → DOWN, and anything else → UNKNOWN.

### Render flow

1. Sensor callbacks only set `have_data` and `last_data_str`. They don't touch widgets.
2. The `render` script (a C++ lambda in `refresh.yaml`) runs:
   - on the minute (`time.on_time seconds: 0`);
   - every 60 s via `interval` until the clock syncs;
   - through `render_now` (2 s debounce, mode `restart`), on boot, on service changes, and on Wi-Fi/API connect or disconnect.
3. `render` works out online/offline, the banner text, and the header, then calls the `dash::` setters for each widget.
4. **Every LVGL invalidation becomes an e-paper refresh**, because LVGL `on_draw_end` triggers `component.update: epd`. The setters in `dashboard.h` compare against current state and skip no-op changes. Keep that property when adding widgets.
5. Ghosting control: `render_count % full_refresh_every == 0` calls `id(epd).force_full_update()` before widgets change, so the first render after boot is full. Also at 03:00, and on a MENU long press (`lvgl.widget.redraw`).

### Charts

- **No LVGL `chart` widget** in this ESPHome (LVGL 9, `LV_USE_CHART` off). Each chart is a 243×96 `canvas` (RGB565 buffer, in PSRAM).
- `dash::hist[3]` holds 31 samples (cpu, mem, gpu), fixed 0-100 scale. The `tick` script (minute tick) calls `shift()`. `render` overwrites the last slot with the live value.
- `draw_chart` writes the buffer directly: white fill, diagonal hatch (`(x + y) % 4 == 0`) under the curve, a 2px line, and a 2px baseline. It only redraws when a rounded sample changed, because a canvas invalidation costs a refresh.
- **History lives in RAM and resets on every reboot/OTA.** HA history isn't reachable from ESPHome. Demo mode prefills fake history in `on_boot`.
- **`NOT WIRED`** shows when the entity substitution still contains `TODO` (checked in `render`). `NO DATA` shows when it's wired but has no samples.

### Offline / stale semantics

- **"HA connected" means `api::global_api_server->is_connected_with_state_subscription()`.** Plain `is_connected()` is true for any API client, including `esphome logs`, and masked a real outage once.
- `offline_since_ms` is set on the first render that sees Wi-Fi or HA down. After `offline_grace_s`, the banner shows `OFFLINE since HH:MM - values are stale`. It uses `millis()` for the duration, so it works before the clock syncs.
- Before any data arrives, the banner shows `Waiting for Home Assistant...`. **If it never clears, the usual cause is that the device isn't added to HA's ESPHome integration.** The Builder add-on alone doesn't subscribe it.
- **Staleness follows the connection, not per-entity age.** HA only pushes on change.
- In demo mode the banner is suppressed, and the title becomes an inverted `DEMO DATA - NOT LIVE`. Demo values must never look plausible. The user once mistook realistic-looking demo IPs for real ones.

## Build, deploy, test

```sh
uv sync                          # first time
cp secrets.yaml.example secrets.yaml   # dummy values are fine for compile
make config                      # validate dev.yaml
make compile                     # full build (first one ~5 min)
make check-secrets               # run before every commit
```

- **Test what the add-on will build:** compile the real stub in an **empty directory** with only `ha-stub.yaml` + `secrets.yaml` copied in. Delete the copy afterwards, because it contains secrets. Building from the repo root can hide path bugs: the config dir then contains `include/`, and remote packages never see that. That's how the `dashboard.h` include bug got through.
- **OTA from a dev machine:**
  - `uv run esphome run ha-stub.local.yaml --device <ip> --no-logs` builds **local, unpushed** packages with real entities and LAN settings. Use this to iterate on hardware before committing.
  - `uv run esphome run ha-stub.yaml ...` builds what the add-on would build (GitHub `main`).
  - `make upload` uses `dev.yaml` with demo data and lacks the LAN settings (NTP, wifi domain). Use it only for quick experiments.
- **Never OTA a build with dummy Wi-Fi credentials.** The device drops off the network, and only USB or the fallback AP recovers it. Check `secrets.yaml` first.
- **Logs:** `uv run esphome logs ha-stub.yaml --device <ip>`. At the default `INFO` level you only see boot and warnings. For sensor traffic, temporarily set `log_level: DEBUG`, then revert. It logs public IPs.
- **Check which firmware is running:** the `compiled on` line at log start. Builds from the add-on vs a dev machine differ by timestamp.
- **Report honestly.** Say what ran on hardware vs only compiled. A clean boot log does not prove the screen shows data. Ask the user to look.

## Accessing Home Assistant

- **The ha-mcp server is available via the `mcporter` CLI:** `mcporter call homeassistant.ha_search_tools query=...`, then `homeassistant.ha_call_read_tool name=<tool> 'arguments={...}'`. Useful read tools: `ha_get_state`, `ha_get_device` (`integration=proxmoxve`), `ha_get_integration` (with `include_diagnostics`), `ha_get_app`.
- **The ESPHome add-on's API returns 403 through `ha_manage_app` (ingress).** Agents can't edit the add-on YAML. Give the user the exact block to paste.
- **Read-only by default.** Ask before changing HA config, integrations, or Proxmox.
- **When reading IP sensors, mask the values** (e.g. print length/shape only). Don't echo the user's public IPs into chat or logs.

## Secrets policy (public repo)

- **Values that live only in the HA-side stub or local gitignored files:** API key, AP/Wi-Fi passwords, entity IDs, service names, `ntp_server`, `wifi.domain`, LAN hostnames/IPs.
- **Repo defaults stay placeholders:** `sensor.TODO_*`, `service-N`, `192.0.2.x`, `x.x.x.x (demo)`.
- **Before each commit:** run `make check-secrets`. Also `git grep` for the user's LAN domain/subnet and API key fragments. The Makefile check can't know those, so check by hand.

## Gotchas (all hit at least once)

- **`id(x)` in lambdas is a macro expanding to `x`.** A local variable named like a widget ID (e.g. `banner`) shadows the global and fails with confusing type errors.
- **`clock` is a reserved ESPHome ID.** The time source is `sntp_time`.
- **`esphome: includes:` in a remote package resolves against the config dir first, then next to the declaring YAML.** So it's `../include/dashboard.h` relative to `packages/ui.yaml`.
- **`!secret` in a remote package falls back to the top-level config's `secrets.yaml`.** Fine, but the packages no longer use secrets anyway.
- **The device's subnet has no internet.** SNTP must use a LAN server via `ntp_server`, and the timezone comes from the build host. Fonts (`gfonts://`) download at **build** time on the add-on host, which does have internet.
- **Fonts must stay `bpp: 1`, at 12 px or more and weight 700 or more.** Thinner or smaller Montserrat breaks up into jagged strokes at 1 bpp. Limit `glyphs:` on large fonts. The value font needs the em dash `—` listed.
- **Colors only `0x000000` / `0xFFFFFF`.** The driver draws anything that isn't pure black as white. A label without a text colour used LVGL's dark-grey default and was invisible (white on white). The page now sets `text_color: 0x000000`, so labels inherit it. Keep explicit colours on anything placed on the black header.
- **Service cell names fit about 9 characters** at 16 px (`long_mode: CLIP`, 104 px). Use short `svcN_name`s.
- **macOS shell:** BSD `sed` has no `\b`, and its `-i` needs `''`. Use `perl -pi -e` or Python for anything non-trivial.
- **Strapping-pin warnings for GPIO45/46** are expected. Those pins are the panel's CS/DC.

## Hardware

- **Pins:** CLK 12, MOSI 11, CS 45, DC 46, RST 47, BUSY 48. GPIO7 is panel power and must be high before the driver starts (`on_boot` priority 600 + 500 ms).
- **Buttons (active-low, pull-up):** MENU 2, EXIT 1, rotary up 6, down 4, press 5. Only MENU long-press is used so far.
- **Driver settings:** `invert_colors: true` is required. Also `update_interval: never` and `auto_clear_enabled: false`, because LVGL drives updates. Partial refresh takes ~0.6 s.
- **PSRAM is `octal`** (confirmed working). The board docs that say `quad` are wrong for this unit.
- **Driver:** [ESPboards/esphome-lvgl-crowpanel-epaper-5.79-4.2](https://github.com/ESPboards/esphome-lvgl-crowpanel-epaper-5.79-4.2), pinned to `319ad7c`. It needs ESPHome ≥ 2025.12 for LVGL idle handling.
- **First flash** from factory firmware must go over USB (add-on "Manual download → Factory format" + web.esphome.io). Everything after that is OTA.

## Extending

- **New data point:**
  1. Add the same `id` to **both** data packages, with `on_value: script.execute: on_data`.
  2. Add a widget in `ui.yaml`.
  3. Set it in `render` through a no-op-skipping setter.
  4. Document the substitution in the README entity table and `ha-stub.yaml.example` (commented).
- **New page / button navigation:**
  - Buttons are already declared in `board.yaml`. Extend them with `!extend <id>`.
  - Each page switch is a full redraw, so it costs a refresh.
  - Keep the offline banner visible on every page.
- **Status as of last session:**
  - Hardware-verified: "1b" layout with live CPU/mem charts, 6 service cells with uptimes, header LED/Wi-Fi/clock, IPs, host uptime, NTP, add-on builds from GitHub.
  - Not yet seen on hardware: offline banner, a DOWN cell, long-run ghosting, MENU long-press.
  - GPU entity doesn't exist yet (shows `—` / `NOT WIRED`).
