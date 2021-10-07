#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

# the first arg is the rebased path to `target_name.clippy` in the generated
# output directory, which is used to form all other output paths.
output="$1"
# after that the positional args are the clippy-driver command and args set
# in the clippy GN template

deps=( $(<"$output.deps") )
transdeps=( $(sort -u "$output.transdeps") )

"${@:2}" -Cpanic=abort -Zpanic_abort_tests -Zno_codegen ${deps[@]} ${transdeps[@]} \
    --emit metadata="$output.rmeta" --emit dep-info="$output.d" \
    --error-format=json --json=diagnostic-rendered-ansi 2>"$output"
