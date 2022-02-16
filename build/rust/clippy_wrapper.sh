#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# the first arg is the rebased path to `target_name.clippy` in the generated
# output directory, which is used to form all other output paths.
output="$1"
shift
# the next arg is the path to jq.
jq="$1"
shift
# the next arg is true or false based on clippy_cause_failure
if [ "$1" == "--fail" ]; then
    fail=$1
    shift
fi
# after that the positional args are the clippy-driver command and args set
# in the clippy GN template

deps=( $(<"$output.deps") )
transdeps=( $(sort -u "$output.transdeps") )

RUSTC_LOG=error "$@" -Cpanic=abort -Zpanic_abort_tests -Zno_codegen \
    ${deps[@]} ${transdeps[@]} --emit metadata="$output.rmeta" \
    --error-format=json --json=diagnostic-rendered-ansi 2>"$output"
result=$?

if [[ $result != 0 && $fail ]]; then
    "$jq" -sr '.[] | select(.level == "error") | .rendered' "$output"
    exit $result
fi
