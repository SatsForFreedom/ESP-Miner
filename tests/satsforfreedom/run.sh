#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
test_bin=$(mktemp /tmp/sff-control.XXXXXX)
trap 'rm -f "$test_bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -I "$repo/main/satsforfreedom" "$repo/main/satsforfreedom/control.c" \
  "$repo/tests/satsforfreedom/control_test.c" -lm -o "$test_bin"
"$test_bin"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -I "$repo/components/asic/include" "$repo/components/asic/crc.c" \
  "$repo/tests/satsforfreedom/uart_test.c" -o "$test_bin"
"$test_bin"
