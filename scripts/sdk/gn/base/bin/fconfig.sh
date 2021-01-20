#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
#### CATEGORY=Device management
### Sets configuration properties persistently so
### they can be used as reasonable defaults for other commands.

set -eu

# Source common functions
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

TOOLS_SHORT_PATH="tools/$(basename "$(get-fuchsia-sdk-tools-dir)")"

function usage {
    fx-error "fconfig.sh is DEPRECATED. Please use ${TOOLS_SHORT_PATH}/fconfig."
  cat << EOF
usage: fconfig.sh [set|get|default|list] propname [value]
    Sets or gets the property values used as defaults for commands.

    set: fconfig.sh set is deprecated. Use "${TOOLS_SHORT_PATH}/fconfig" to configure
        properties. This script only works for reading values.

    get: Prints the value of the property or empty string if not found.
    default: Restores the given property to the default value, or unsets.
    list: Lists prop=value one per line.

    propname: One of the predefined properties: $(get-fuchsia-property-names)).
    value: if using setting, the value to set to the property. Otherwise ignored.
EOF
}

function _do_get {
    if is-valid-fuchsia-property "$1"; then
        get-fuchsia-property "$1"
    else
        fx-error "Invalid property name: $1"
        return 1
    fi
}

function _do_list {
    for p in $(get-fuchsia-property-names); do
        echo "${p}=$(get-fuchsia-property "$p")"
    done
}

CMD=""
PROPNAME=""

if (( "$#" >= 1 )); then
    CMD="${1}"
else
    usage
    exit 1
fi
shift

if [[ "${CMD}" == "set" || "${CMD}" == "default" ]]; then
    fx-error "fconfig.sh default/set are no longer supported. Use ${TOOLS_SHORT_PATH}/fconfig instead."
     exit 1
elif [[ "${CMD}" == "get" ]]; then
    if (( "$#" == 1 )); then
        PROPNAME="${1}"
    else
        usage
        exit 1
    fi
    _do_get "${PROPNAME}"
elif [[ "${CMD}" == "list" ]]; then
    if (( "$#" != 0 )); then
        usage
        exit 1
    fi
    _do_list
else
    usage
    exit 1
fi
