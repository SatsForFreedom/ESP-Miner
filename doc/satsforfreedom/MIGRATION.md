# SatsForFreedom migration

Upstream base: `c75aa99dc867db6ed1f38c7add113c452f263633`.
Legacy working branch: `1822dac64133be24498e28bd418bf2f442dbc4e9`.
Locally recorded published branch: `ed3e73d16db36fd8c55a696cb79499fcfe052f12`.
The additional published commits are upstream merges (including BM1368 and pool-password support); those capabilities already exist in this base. No remote branches have been rewritten.

## Structure for future rebases

- `main/satsforfreedom/`: board profile, voltage checks, power control, telemetry, and settings validation.
- `components/asic/bm1397_satsforfreedom.inc`: private driver startup/readback and baud validation helpers.
- `main/http_server/axe-os/src/app/components/satsforfreedom/`: standalone settings and telemetry panel.
- `configs/config-satsforfreedom-2A.csv`: explicit board selection. Board-specific electrical changes activate only for NVS `boardversion=2.A`.
- `tests/satsforfreedom/`: host control/calibration tests, independent of ESP-IDF.
- `SatsForFreedom.code-workspace`: old local editor preferences; `.vscode/settings.json` in the new clone remains intact.

Integration edits stay at the existing board initialization, regulator, ASIC startup, power task, API, and display boundaries. Do not merge the old branch wholesale: it reintroduces obsolete firmware and frontend architecture. Rebase the modern branch's focused commits onto upstream, then run the host tests, frontend checks, and the firmware build.

## Feature disposition

| Legacy customization | Modern implementation / replacement |
| --- | --- |
| PCB 2.A selection, ADC channel 3, GPIO14 active-low power enable, GPIO10 BI | Board profile and small ADC/regulator integration hooks; ADC initializes after board selection. |
| DS4432 33k/22k/100k resistor calibration | Pure `sff_voltage_code`; preserves calibration, prevents nominal-voltage unsigned underflow, rejects nonfinite/out-of-range values. |
| 1.5 V upper bound and measured-voltage startup validation | Board 2.A regulator/API checks; ten ADC samples, 150 mV tolerance, power off and hardware fault on mismatch. Compares with requested voltage, not an obsolete compile-time default. |
| Uncommitted regulator startup change | Enable before setting voltage, wait 100 ms; retains removal of the premature PGOOD check. |
| Configurable power target and maximum frequency | `powerLimitMilliwatts` controls BM1397 frequency; upstream `frequency` is the ceiling, `actualFrequency` is measured/applied frequency. Board 2.A defaults to 12 W. |
| Power/temperature cascade and sensor filtering | Bounded controller with filtered power and immediate response to excess power; upstream's stronger thermal shutdown remains authoritative. Raw thermal protection readings are deliberately not smoothed. |
| Live voltage adjustment | Upstream power task already applies voltage settings without reboot; no HTTP handler performs direct I2C writes. |
| BM1397 chip detection and init register verification | Upstream chip detection plus length/preamble/CRC/register/value-checked startup readbacks on board 2.A. |
| Fastest valid UART and retry initialization | Board 2.A probes descending baud rates. Failed probes trigger hardware reset before retry; bounded to 27 attempts instead of an infinite loop. |
| Initial 50 MHz and improved PLL calculation | Explicit 50 MHz startup on board 2.A; retain upstream PLL solver/ramp and nonce-space update instead of copying the old formula. |
| Restart after repeated null ASIC results | BM1397 restarts after ten minutes without valid packets while active. Pause/fault/self-test suppresses the watchdog. Ignored packets no longer count as ten consecutive failures. |
| Static job validity table, allocation checks, nonce validation | Retain upstream job pool, bounded stack packets, allocation handling, job locks, and CRC checks. The old string-based coinbase/hash allocation paths no longer exist. |
| Mean efficiency and hashrate warmup | Smoothed efficiency is exposed in API/panel and OLED. Keep upstream register-based hashrate and 1m/10m/1h averages instead of the old share-based estimator. |
| PCB revision, session ID, actual frequency | Upstream `boardVersion`/`actualFrequency`; new `sessionId` and `meanEfficiency` in the dedicated panel. Session ID is V1 extranonce; cleared on disconnect. |
| OLED remap and padded text | Board 2.A monochrome mirror setting; upstream LVGL labels replace the old fixed-size padded string buffers. |
| Slower input polling | Upstream input handling replaces the removed polling task. No additional 500 ms UI delay. |
| Pool password and BM1368 integration | Already present upstream. |
| Removal of old latest-release settings display | Old settings layout is gone; retain upstream firmware-update UI. |
| Logging severity / heap tracing scaffolding | Retain current upstream error handling; obsolete unused tracing buffer and cosmetic logging-only edits are not reintroduced. |
| USB-JTAG debug profiles and editor associations | Debug profiles appended; local preferences preserved in the workspace file. |

## Settings compatibility

The old NVS `asicMaxPower` key is retained, in **milliwatts**. The old `asicMaxfreq` value migrates to upstream's float frequency setting only when the new setting is absent. Existing upstream frequency values take precedence.

The old HTTP `maxPower` name cannot be retained: upstream uses it for the board rating in **watts**. Use `powerLimitMilliwatts`. Likewise use `frequency` for the configured ceiling and `actualFrequency` for the running clock; do not overwrite upstream API meanings with legacy aliases.

Power control is a feedback target, not a hardware current limiter. Minimum running frequency is 50 MHz, so some targets cannot be achieved. Upstream pause, thermal shutdown, and hardware faults take priority. Board 2.A is limited to 50–580 MHz and 1000–1500 mV.

Board 2.A must be explicitly selected in NVS. The old compile-time `CONFIG_BITAXE2_A` does not exist upstream; an old device whose NVS still says `102` or `2.2` must have its board identity corrected before using the new firmware. The supplied CSV is a provisioning template; preserve your own pool, WiFi, and identity settings when provisioning.

## Verification

Run `bash tests/satsforfreedom/run.sh` from the repository root.
Frontend requires Node 22+; run `npm run generate:api` and `npm run build` in `main/http_server/axe-os`.
Firmware requires ESP-IDF 6.0.2 as specified by upstream `AGENTS.md`.
Hardware checks must cover regulator startup/readback, OLED orientation, register readback and baud fallback, live voltage changes, power-target convergence, pause/resume, and thermal shutdown.

This migration has not been flashed to hardware. See `VALIDATION.md` for the checks actually completed and outstanding limitations.
