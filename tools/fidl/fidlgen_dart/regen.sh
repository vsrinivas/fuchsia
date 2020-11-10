#!/usr/bin/env bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


if [ ! -x "${FUCHSIA_BUILD_DIR}" ]; then
    echo "error: did you fx exec? missing \$FUCHSIA_BUILD_DIR" 1>&2
    exit 1
fi

FIDLGEN="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_dart"
if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen missing; maybe fx clean-build x64?" 1>&2
    exit 1
fi

DARTFMT="${FUCHSIA_DIR}/prebuilt/third_party/dart/linux-x64/bin/dartfmt"
if [ ! -x "${DARTFMT}" ]; then
    DARTFMT="${FUCHSIA_DIR}/prebuilt/third_party/dart/mac-x64/bin/dartfmt"
    if [ ! -x "${DARTFMT}" ]; then
        echo "error: dartfmt missing; did its location change? Looking in ${DARTFMT}" 1>&2
        exit 1
    fi
fi

FIDLC_IR_DIR="${FUCHSIA_DIR}/zircon/tools/fidl/goldens"
GOLDENS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/goldens"
GOLDENS=()

# fresh regen
find "${GOLDENS_DIR}" -type f -not -name 'BUILD.gn' -exec rm {} \;

for src_path in $(find "${FIDLC_IR_DIR}" -name '*.test.json.golden'); do
    src_name="$( basename "${src_path}" | sed -e 's/\.json\.golden$//g' )"

    # TODO(fxb/45006): Skipping due to issue with representation of binary
    # operators.
    if [ "constants.test" = "${src_name}" ]; then
        continue
    fi

    json_name="${src_name}.json"
    dart_async_name="${json_name}_async.dart.golden"
    dart_test_name="${json_name}_test.dart.golden"

    GOLDENS+=(
      $json_name,
      $dart_async_name,
      $dart_test_name,
    )

    echo -e "\033[1mexample: ${json_name}\033[0m"
    cp "${src_path}" "${GOLDENS_DIR}/${json_name}"
    ${FIDLGEN} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -output-async "${GOLDENS_DIR}/${dart_async_name}" \
        -output-test "${GOLDENS_DIR}/${dart_test_name}" \
        -dartfmt "$DARTFMT"
done

> "${GOLDENS_DIR}/goldens.txt"
printf "%s\n" "${GOLDENS[@]//,}" | sort >> "${GOLDENS_DIR}/goldens.txt"
