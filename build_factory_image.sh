#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Prints command usage and prerequisites for generating a factory image.
usage() {
    cat <<'EOF'
Build a factory image containing firmware and the selected board's NVS config.

Usage:
  ./build_factory_image.sh CONFIG.csv [OUTPUT.bin]

Run this after sourcing the ESP-IDF export.sh script and building the firmware.
This script only generates images; it does not flash a device.
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage >&2
    exit 2
fi

CONFIG_CSV="$1"
if [[ "$CONFIG_CSV" != /* ]]; then
    CONFIG_CSV="$PWD/$CONFIG_CSV"
fi
if [[ ! -f "$CONFIG_CSV" ]]; then
    echo "ERROR: config CSV not found: $CONFIG_CSV" >&2
    exit 1
fi

CONFIG_STEM="$(basename -- "$CONFIG_CSV" .csv)"
CONFIG_STEM="${CONFIG_STEM#config-}"
if [[ $# -eq 2 ]]; then
    OUTPUT_BIN="$2"
else
    OUTPUT_BIN="esp-miner-factory-${CONFIG_STEM}.bin"
fi
if [[ "$OUTPUT_BIN" != /* ]]; then
    OUTPUT_BIN="$PWD/$OUTPUT_BIN"
fi

if [[ -z "${IDF_PATH:-}" || ! -d "$IDF_PATH" ]]; then
    echo "ERROR: IDF_PATH is unset. Source ESP-IDF's export.sh first." >&2
    exit 1
fi

NVS_GENERATOR="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
if [[ ! -f "$NVS_GENERATOR" ]]; then
    echo "ERROR: NVS partition generator not found: $NVS_GENERATOR" >&2
    exit 1
fi

if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
    PYTHON_BIN="$IDF_PYTHON_ENV_PATH/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python3)"
else
    echo "ERROR: Python 3 not found." >&2
    exit 1
fi

for input in \
    "$SCRIPT_DIR/build/bootloader/bootloader.bin" \
    "$SCRIPT_DIR/build/partition_table/partition-table.bin" \
    "$SCRIPT_DIR/build/esp-miner.bin" \
    "$SCRIPT_DIR/build/ota_data_initial.bin"; do
    if [[ ! -f "$input" ]]; then
        echo "ERROR: required build output missing: $input" >&2
        echo "Run idf.py build first." >&2
        exit 1
    fi
done

if ! command -v esptool.py >/dev/null 2>&1; then
    echo "ERROR: esptool.py not found. Source ESP-IDF's export.sh first." >&2
    exit 1
fi

# The NVS partition is 0x6000 bytes at flash offset 0x9000 (partitions.csv).
"$PYTHON_BIN" "$NVS_GENERATOR" generate "$CONFIG_CSV" "$SCRIPT_DIR/config.bin" 0x6000

cd "$SCRIPT_DIR"
./merge_bin.sh -c "$OUTPUT_BIN"
