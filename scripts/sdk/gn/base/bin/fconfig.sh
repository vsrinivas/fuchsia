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

function usage {
  cat << EOF
usage: fconfig.sh [set|get|default|list] propname [value]
    Sets or gets the property values used as defaults for commands.

    set: Sets the property to the given value.
    get: Prints the value of the property or empty string if not found.
    default: Restores the given property to the default value, or unsets.
    list: Lists prop=value one per line.

    propname: One of the predefined properties: $(get-fuchsia-property-names)).
    value: if using setting, the value to set to the property. Otherwise ignored.
EOF
}

function _do_set {
    if is-valid-fuchsia-property "$1"; then
        set-fuchsia-property "$1" "$2"
    else
        fx-error "Invalid property name: $1"
        return 1
    fi
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
VALUE=""

if (( "$#" >= 1 )); then
    CMD="${1}"
else
    usage
    exit 1
fi
shift

if [[ "${CMD}" == "set" ]]; then
    if (( "$#" >= 2 )); then
        PROPNAME="${1}"
        shift
        VALUE="$*"
    else
        usage
        exit 1
    fi
    _do_set "${PROPNAME}" "${VALUE}"
elif [[ "${CMD}" == "get" ]]; then
    if (( "$#" == 1 )); then
        PROPNAME="${1}"
    else
        usage
        exit 1
    fi
    _do_get "${PROPNAME}"
elif [[ "${CMD}" == "default" ]]; then
    if (( "$#" == 1 )); then
        PROPNAME="${1}"
    else
        usage
        exit 1
    fi
    _do_set "${PROPNAME}" ""
elif [[ "${CMD}" == "list" ]]; then
    if (( "$#" != 0 )); then
        usage
        exit 1
    fi
    _do_list
fi
