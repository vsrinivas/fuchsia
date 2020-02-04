#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

usage() {
    echo "usage: $(basename ${0}) output command [args...]"
    echo "Runs a command with the provided arguments, and verifies a non-zero exit code."
    echo "It also writes the exit code to the provided output file."
}

if [[ $# -lt 1 ]]; then
    usage
    exit 0
fi

output=$1
shift
cmd=$1
shift

if [[ ! -x "${cmd}" ]]; then
    if [[ "$(which ${cmd})" = "" ]]; then
        echo "Error: \"${cmd}\" could not be found or is not executable."
        exit -1
    fi
fi

set +e
${cmd} $@ > /dev/null 2>&1
ret=$?
set -e

if [[ ${ret} = 0 ]]; then
    echo "Error: we expected an error when running \"${cmd} $@\" but did not encounter one."
    exit -1
fi

echo ${ret} > ${output}
