# Migration validation

Completed in the temporary migration checkout:

- Host calibration/control tests compiled with C11, `-Wall -Wextra -Werror`, and undefined-behavior sanitizer.
- Calibration tests cover nonfinite/out-of-range inputs, nominal-voltage underflow, and the complete 1000–1500 mV range.
- Power-control tests cover response direction, thermal reduction, invalid sensors, frequency bounds, and convergence in a simple load model.
- UART tests use the actual upstream CRC function and cover valid, truncated, and corrupted register replies plus baud fallback.
- OpenAPI client regenerated with `npm run generate:api` as part of the production build.
- AxeOS production compilation and compressed asset generation completed with temporary Node 22.23.3.
- `git diff --check` passed.
- Legacy Git bundle verified; both original working-tree patches saved separately.

Limitations:

- Firmware has **not** been compiled or flashed. The installed ESP-IDF is 5.1.1; this upstream checkout requires 6.0.2. Firmware compilation and real board 2.A tests remain required before use.
- UART tests mock transport and cannot establish that a physical chip supports each register readback at the selected baud.
- The power test uses a simple simulated load; it does not establish hardware control-loop stability or an instantaneous electrical limit.
- No browser-based frontend unit test suite was run (no Chrome/Chromium found); production Angular template/type compilation passed.
- Upstream `npm ci` fails because its lockfile lacks chokidar/readdirp entries. Validation used `npm install --no-save --package-lock=false --ignore-scripts` in the temporary checkout. No dependency manifests or lockfiles were changed; this is not a lockfile-reproducible build.

No firmware was deployed, no remote was pushed, and no history was rewritten.
