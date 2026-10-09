#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="${IDF_PATH:-/Users/sinan/esp/v5.5.1/esp-idf}"
cjson_dir="$idf_dir/components/json/cJSON"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/esp0-tests.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT
compiler="${CC:-/usr/bin/cc}"
# macOS deprecates sprintf used by the bundled third-party cJSON implementation.
# Keep strict warnings for project code; suppress only that dependency warning.
"$compiler" -std=c11 -fsanitize=address,undefined -Wno-deprecated-declarations \
    -I"$cjson_dir" -c "$cjson_dir/cJSON.c" -o "$test_dir/cJSON.o"
"$compiler" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/main" -I"$cjson_dir" \
    "$project_dir/tests/test_controller.c" \
    "$project_dir/main/controller_logic.c" "$project_dir/main/wiz_protocol.c" \
    "$test_dir/cJSON.o" -lm -o "$test_dir/test_controller"
"$test_dir/test_controller"
