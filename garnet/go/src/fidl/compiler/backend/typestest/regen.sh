#!/usr/bin/env bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

source "${FUCHSIA_DIR}/tools/devshell/lib/platform.sh"

if [ ! -x "${FUCHSIA_BUILD_DIR}" ]; then
    echo "error: did you fx exec? missing \$FUCHSIA_BUILD_DIR" 1>&2
    exit 1
fi

FIDLGEN_HLCPP="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_hlcpp"
if [ ! -x "${FIDLGEN_HLCPP}" ]; then
    echo "error: fidlgen_hlcpp missing; maybe fx clean-build?" 1>&2
fi

FIDLGEN_LLCPP="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_llcpp"
if [ ! -x "${FIDLGEN_LLCPP}" ]; then
    echo "error: fidlgen_llcpp missing; maybe fx clean-build?" 1>&2
    exit 1
fi

FIDLGEN_GO="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_go"
if [ ! -x "${FIDLGEN_GO}" ]; then
    echo "error: fidlgen_go missing; maybe fx clean-build?" 1>&2
    exit 1
fi

FIDLGEN_RUST="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_rust"
if [ ! -x "${FIDLGEN_RUST}" ]; then
    echo "error: fidlgen_rust missing; maybe fx clean-build?" 1>&2
    exit 1
fi

FIDLGEN_LIBFUZZER="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_libfuzzer"
if [ ! -x "${FIDLGEN_LIBFUZZER}" ]; then
    echo "error: fidlgen_libfuzzer missing; maybe fx clean-build?" 1>&2
    exit 1
fi

FIDLGEN_SYZ="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_syzkaller"
if [ ! -x "${FIDLGEN_SYZ}" ]; then
    echo "error: fidlgen_syzkaller missing; maybe fx clean-build?" 1>&2
    exit 1
fi

EXAMPLE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
EXAMPLE_DIR="$( realpath "$EXAMPLE_DIR" )"
GOLDENS_DIR="$( realpath "${EXAMPLE_DIR}/../goldens" )"
FIDLC_IR_DIR="${FUCHSIA_DIR}/zircon/tools/fidl/goldens"
GOLDENS=()

# fresh regen
find "${GOLDENS_DIR}" -type f -not -name 'BUILD.gn' -exec rm {} \;
find "${GOLDENS_DIR}"/* -maxdepth 0 -type d -exec rm -r {} \;

# base all paths in GOLDENS_DIR
mkdir -p "${GOLDENS_DIR}"
cd "${GOLDENS_DIR}"

while IFS= read -r -d '' src_path
do
    src_name="$( basename "${src_path}" | sed -e 's/\.json\.golden$//g' )"

    # TODO(fxbug.dev/45006): Skipping due to issue with representation of binary
    # operators.
    if [ "constants.test" = "${src_name}" ]; then
        continue
    fi

    ensure_goldens_dir () {
        local goldens_sub_dir="${GOLDENS_DIR}/$1"
        mkdir -p "$goldens_sub_dir"
        realpath "$goldens_sub_dir" --relative-to="$GOLDENS_DIR"
    }

    echo -e "\033[1mexample: ${src_name}\033[0m"

    # Note that each backend needs to have a unique suffix in the generated
    # file names, because the generated files are collated into one flat
    # directory and filtered by various fidlgen golden tests.
    # See //garnet/go/src/fidl/compiler/backend/goldens/BUILD.gn

    json_name="${src_name}.json"
    echo "  json ir: ${src_name} > ${json_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'json')"
    json_path="${GOLDENS_SUB_DIR}/$json_name"
    cp "${src_path}" "$json_path"
    GOLDENS+=(
      "$json_path"
    )

    hlcpp_header_name=${json_name}.h
    hlcpp_test_header_name=${json_name}_test_base.h
    hlcpp_source_name=${json_name}.cc
    echo "  hlcpp: ${json_name} > ${hlcpp_header_name}, ${hlcpp_source_name}, and ${hlcpp_test_header_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'hlcpp')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${hlcpp_header_name}.golden"
      "${GOLDENS_SUB_DIR}/${hlcpp_test_header_name}.golden"
      "${GOLDENS_SUB_DIR}/${hlcpp_source_name}.golden"
    )
    ${FIDLGEN_HLCPP} \
        -json "$json_path" \
        -output-base "${GOLDENS_SUB_DIR}/${json_name}" \
        -include-base "${GOLDENS_SUB_DIR}" \
        -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
    mv "${GOLDENS_SUB_DIR}/${hlcpp_header_name}" "${GOLDENS_SUB_DIR}/${hlcpp_header_name}.golden"
    mv "${GOLDENS_SUB_DIR}/${hlcpp_source_name}" "${GOLDENS_SUB_DIR}/${hlcpp_source_name}.golden"
    mv "${GOLDENS_SUB_DIR}/${hlcpp_test_header_name}" "${GOLDENS_SUB_DIR}/${hlcpp_test_header_name}.golden"

    # HLCPP codegen variation: natural types only
    hlcpp_header_name=${json_name}.natural_types.h
    hlcpp_source_name=${json_name}.natural_types.cc
    echo "  hlcpp natural types: ${json_name} > ${hlcpp_header_name}, ${hlcpp_source_name}, and ${hlcpp_test_header_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'hlcpp_natural_types')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${hlcpp_header_name}.golden"
      "${GOLDENS_SUB_DIR}/${hlcpp_source_name}.golden"
    )
    ${FIDLGEN_HLCPP} \
        -json "$json_path" \
        -experimental-split-generation-domain-objects \
        -output-base "${GOLDENS_SUB_DIR}/${json_name}.natural_types" \
        -include-base "${GOLDENS_SUB_DIR}" \
        -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
    mv "${GOLDENS_SUB_DIR}/${hlcpp_header_name}" "${GOLDENS_SUB_DIR}/${hlcpp_header_name}.golden"
    mv "${GOLDENS_SUB_DIR}/${hlcpp_source_name}" "${GOLDENS_SUB_DIR}/${hlcpp_source_name}.golden"

    llcpp_header_name=${json_name}.llcpp.h
    llcpp_source_name=${json_name}.llcpp.cc
    echo "  llcpp: ${json_name} > ${llcpp_header_name} and ${llcpp_source_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'llcpp')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${llcpp_header_name}.golden"
      "${GOLDENS_SUB_DIR}/${llcpp_source_name}.golden"
    )
    ${FIDLGEN_LLCPP} \
        -json "$json_path" \
        -header "${GOLDENS_SUB_DIR}/${llcpp_header_name}" \
        -source "${GOLDENS_SUB_DIR}/${llcpp_source_name}" \
        -include-base "${GOLDENS_SUB_DIR}" \
        -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
    mv "${GOLDENS_SUB_DIR}/${llcpp_header_name}" "${GOLDENS_SUB_DIR}/${llcpp_header_name}.golden"
    mv "${GOLDENS_SUB_DIR}/${llcpp_source_name}" "${GOLDENS_SUB_DIR}/${llcpp_source_name}.golden"

    # libfuzzer expects at least one nonempty protocol definition or it will fail.
    # Add sources that contain a protocol with at least one method.
    # Remove libfuzzer golden files from a source that (no longer) contains a golden file.
    # The regex \[[^]] means a literal [ followed by anything except ].
    libfuzzer_header_name=${json_name}.libfuzzer.h
    libfuzzer_source_name=${json_name}.libfuzzer.cc
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'libfuzzer')"
    if tr -d '[:space:]' < "${src_path}" | grep -q '"methods":\[[^]]' ; then
        echo "  libfuzzer: ${json_name} > ${libfuzzer_header_name}, and ${libfuzzer_source_name}"
        GOLDENS+=(
            "${GOLDENS_SUB_DIR}/${libfuzzer_header_name}.golden"
            "${GOLDENS_SUB_DIR}/${libfuzzer_source_name}.golden"
        )
        ${FIDLGEN_LIBFUZZER} \
            -json "$json_path" \
            -output-base "${GOLDENS_SUB_DIR}/${json_name}" \
            -include-base "${GOLDENS_SUB_DIR}" \
            -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
        mv "${GOLDENS_SUB_DIR}/${json_name}.h" "${GOLDENS_SUB_DIR}/${libfuzzer_header_name}.golden"
        mv "${GOLDENS_SUB_DIR}/${json_name}.cc" "${GOLDENS_SUB_DIR}/${libfuzzer_source_name}.golden"
    else
        if [[ -f "${GOLDENS_SUB_DIR}/${libfuzzer_header_name}.golden" ]]; then
            rm "${GOLDENS_SUB_DIR}/${libfuzzer_header_name}.golden"
        fi
        if [[ -f "${GOLDENS_SUB_DIR}/${libfuzzer_source_name}.golden" ]]; then
            rm "${GOLDENS_SUB_DIR}/${libfuzzer_source_name}.golden"
        fi
    fi

    go_impl_name=${json_name}.go
    echo "  go: ${json_name} > ${go_impl_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'go')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${go_impl_name}.golden"
    )
    ${FIDLGEN_GO} \
        -json "$json_path" \
        -output-impl "${GOLDENS_SUB_DIR}/${go_impl_name}.golden"

    rust_name=${json_name}.rs
    echo "  rust: ${json_name} > ${rust_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'rust')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${rust_name}.golden"
    )
    ${FIDLGEN_RUST} \
        -json "$json_path" \
        -output-filename "${GOLDENS_SUB_DIR}/${rust_name}.golden" \
        -rustfmt "${PREBUILT_RUST_TOOLS_DIR}/bin/rustfmt"

    # TODO(fxbug.dev/45007): Syzkaller does not support enum member references in struct
    # defaults.
    if [ "struct_default_value_enum_library_reference.test" = "${src_name}" ]; then
        continue
    fi
    syzkaller_name=${json_name}.syz.txt
    echo "  syzkaller: ${json_name} > ${syzkaller_name}"
    GOLDENS_SUB_DIR="$(ensure_goldens_dir 'syzkaller')"
    GOLDENS+=(
      "${GOLDENS_SUB_DIR}/${syzkaller_name}.golden"
    )
    ${FIDLGEN_SYZ} \
        -json "$json_path" \
        -output-syz "${GOLDENS_SUB_DIR}/${syzkaller_name}.golden"
done <   <(find "${FIDLC_IR_DIR}" -name '*.test.json.golden' -print0)

# Set `LC_ALL=C` during sorting to make it locale-independent.
true > "${GOLDENS_DIR}/goldens.txt"
printf "%s\n" "${GOLDENS[@]//,}" | LC_ALL=C sort >> "${GOLDENS_DIR}/goldens.txt"

true > "${GOLDENS_DIR}/OWNERS"
find "${EXAMPLE_DIR}/../.." \
    "${FUCHSIA_DIR}/tools/fidl/fidlgen_"{go,hlcpp,rust,libfuzzer,syzkaller} \
    -name 'OWNERS' -exec cat {} \; \
    | grep -v -e "^#" | awk 'NF' | LC_ALL=C sort -u > "${GOLDENS_DIR}/OWNERS"
echo "" >> "${GOLDENS_DIR}/OWNERS"
echo "# COMPONENT: FIDL>Testing" >> "${GOLDENS_DIR}/OWNERS"
