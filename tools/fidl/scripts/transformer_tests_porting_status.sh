#!/bin/bash

set -eufo pipefail

normalize() {
    sed 's/_//g' | tr A-Z a-z | sed 's/^transformer//;s/case1//'
}

show() {
    if [[ -z "$("$@" | tee /dev/tty)" ]]; then
        echo "(nothing)"
    fi
}

gidl="$FUCHSIA_DIR/tools/fidl/gidl-conformance-suite/transformer.gidl"
unit="$FUCHSIA_DIR/zircon/system/utest/fidl/transformer_tests.c"

gidl_tests=$(
    grep '^success' "$gidl" | sed 's/.*"\(.*\)".*/\1/' | normalize | sort)
unit_tests=$(
    grep 'RUN_TEST' "$unit" | sed 's/.*(\(.*\)).*/\1/' | normalize | sort)

echo "* Tests in GIDL:"
echo
echo "$gidl_tests"
echo
echo "* Tests in utest/fidl:"
echo
echo "$unit_tests"
echo
echo "* Tests that only exist in gidl:"
echo
show comm -23 <(echo "$gidl_tests") <(echo "$unit_tests")
echo
echo "* Tests that only exist in utest/fidl:"
echo
show comm -13 <(echo "$gidl_tests") <(echo "$unit_tests")
