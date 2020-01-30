#!/bin/bash

declare -r SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

"$SCRIPT_SRC_DIR/script_runner.sh" fuchsia-common-tests.sh
exit $?