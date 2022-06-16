#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Copy the cpuperf output files from the target.
# Requires a workable fx program to be in $PATH.

set -e

declare -r SCRIPT=$(basename $0)

if [ $# -ne 2 ]
then
    echo "Usage: $SCRIPT <session-result-spec-path> <output-directory>" >&2
    echo "Example: $SCRIPT /data/cpuperf-test.cpsession ." >&2
    exit 1
fi

declare -r RESULT_SPEC_PATH=$1
declare -r OUTPUT_DIR=$2

if [ ! -d "$OUTPUT_DIR" ]
then
    echo "Not a directory: $OUTPUT_DIR" >&2
    exit 1
fi

RESULT_SPEC_BASENAME=$(basename "$RESULT_SPEC_PATH")
LOCAL_RESULT_SPEC_PATH="${OUTPUT_DIR}/${RESULT_SPEC_BASENAME}"

# Keep debugging easy during development.
set -x

fx cp --to-host "${RESULT_SPEC_PATH}" "${LOCAL_RESULT_SPEC_PATH}"

function extract_field() {
    local -r PREFIX=$1
    local -r VALUE=$2
    grep -o "${PREFIX}${VALUE}" "${LOCAL_RESULT_SPEC_PATH}" | \
        sed -e "s,$PREFIX,,"
}

# Pull out the num_iterations, num_traces, output_path_prefix fields.
declare -r NUM_ITERATIONS=$(extract_field '"num_iterations":' '[0-9]*')
declare -r NUM_TRACES=$(extract_field '"num_traces":' '[0-9]*')
declare -r OUTPUT_PATH_PREFIX=$(extract_field '"output_path_prefix":"' '[^"]*')

iter=0
while [[ $iter -lt $NUM_ITERATIONS ]]
do
    trace=0
    while [[ $trace -lt $NUM_TRACES ]]
    do
        file="${OUTPUT_PATH_PREFIX}.${iter}.${trace}.cpuperf"
        local_file="${OUTPUT_DIR}/$(basename "$file")"
        fx cp --to-host "$file" "$local_file"
        trace=$(($trace + 1))
    done
    iter=$(($iter + 1))
done
