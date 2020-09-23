#!/usr/bin/env bash
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
EXAMPLE_DIR="$( echo $EXAMPLE_DIR | sed -e "s+${FUCHSIA_DIR}/++" )"
GOLDENS_DIR="${EXAMPLE_DIR}/../goldens"
FIDLC_IR_DIR="${FUCHSIA_DIR}/zircon/tools/fidl/goldens"
GOLDENS=()

# base all paths in FUCHSIA_DIR 
cd "${FUCHSIA_DIR}"

# fresh regen
find "${GOLDENS_DIR}" -type f -not -name 'BUILD.gn' -exec rm {} \;

for src_path in `find "${FIDLC_IR_DIR}" -name '*.test.json.golden'`; do
    src_name="$( basename "${src_path}" | sed -e 's/\.json\.golden$//g' )"

    # TODO(fxbug.dev/45006): Skipping due to issue with representation of binary
    # operators.
    if [ "constants.test" = "${src_name}" ]; then
        continue
    fi

    json_name="${src_name}.json"
    cpp_header_name=${json_name}.h
    cpp_test_header_name=${json_name}_test_base.h
    cpp_source_name=${json_name}.cc
    llcpp_header_name=${json_name}.llcpp.h
    llcpp_source_name=${json_name}.llcpp.cc
    libfuzzer_header_name=${json_name}.libfuzzer.h
    libfuzzer_source_name=${json_name}.libfuzzer.cc
    go_impl_name=${json_name}.go
    rust_name=${json_name}.rs
    syzkaller_name=${json_name}.syz.txt

    GOLDENS+=(
      "$json_name",
      "${cpp_header_name}.golden",
      "${cpp_test_header_name}.golden",
      "${cpp_source_name}.golden",
      "${llcpp_header_name}.golden",
      "${llcpp_source_name}.golden",
      "${go_impl_name}.golden",
      "${rust_name}.golden",
    )

    echo -e "\033[1mexample: ${src_name}\033[0m"

    echo "  json ir: ${src_name} > ${json_name}"
    cp "${src_path}" "${GOLDENS_DIR}/${json_name}"

    echo "  cpp: ${json_name} > ${cpp_header_name}, ${cpp_source_name}, and ${cpp_test_header_name}"
    ${FIDLGEN_HLCPP} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -output-base "${GOLDENS_DIR}/${json_name}" \
        -include-base "${GOLDENS_DIR}" \
        -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
    mv "${GOLDENS_DIR}/${cpp_header_name}" "${GOLDENS_DIR}/${cpp_header_name}.golden"
    mv "${GOLDENS_DIR}/${cpp_source_name}" "${GOLDENS_DIR}/${cpp_source_name}.golden"
    mv "${GOLDENS_DIR}/${cpp_test_header_name}" "${GOLDENS_DIR}/${cpp_test_header_name}.golden"

    echo "  llcpp: ${json_name} > ${llcpp_header_name} and ${llcpp_source_name}"
    ${FIDLGEN_LLCPP} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -header "${GOLDENS_DIR}/${llcpp_header_name}" \
        -source "${GOLDENS_DIR}/${llcpp_source_name}" \
        -include-base "${GOLDENS_DIR}" \
        -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
    mv "${GOLDENS_DIR}/${llcpp_header_name}" "${GOLDENS_DIR}/${llcpp_header_name}.golden"
    mv "${GOLDENS_DIR}/${llcpp_source_name}" "${GOLDENS_DIR}/${llcpp_source_name}.golden"

    # libfuzzer expects at least one nonempty protocol definition or it will fail.
    # Add sources that contain a protocol with at least one method.
    # Remove libfuzzer golden files from a source that (no longer) contains a golden file.
    # The regex \[[^]] means a literal [ followed by anything except ].
    if tr -d '[:space:]' < "${src_path}" | grep -q '"methods":\[[^]]' ; then
        GOLDENS+=(
            "${libfuzzer_header_name}.golden",
            "${libfuzzer_source_name}.golden",
        )
        echo "  libfuzzer: ${json_name} > ${libfuzzer_header_name}, and ${libfuzzer_source_name}"
        ${FIDLGEN_LIBFUZZER} \
            -json "${GOLDENS_DIR}/${json_name}" \
            -output-base "${GOLDENS_DIR}/${json_name}" \
            -include-base "${GOLDENS_DIR}" \
            -clang-format-path "${PREBUILT_CLANG_DIR}/bin/clang-format"
        mv "${GOLDENS_DIR}/${cpp_header_name}" "${GOLDENS_DIR}/${libfuzzer_header_name}.golden"
        mv "${GOLDENS_DIR}/${cpp_source_name}" "${GOLDENS_DIR}/${libfuzzer_source_name}.golden"
    else
        if [[ -f "${GOLDENS_DIR}/${libfuzzer_header_name}.golden" ]]; then
            rm "${GOLDENS_DIR}/${libfuzzer_header_name}.golden"
        fi
        if [[ -f "${GOLDENS_DIR}/${libfuzzer_source_name}.golden" ]]; then
            rm "${GOLDENS_DIR}/${libfuzzer_source_name}.golden"
        fi
    fi

    echo "  go: ${json_name} > ${go_impl_name}"
    ${FIDLGEN_GO} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -output-impl "${GOLDENS_DIR}/${go_impl_name}.golden"

    echo "  rust: ${json_name} > ${rust_name}"
    ${FIDLGEN_RUST} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -output-filename "${GOLDENS_DIR}/${rust_name}.golden" \
        -rustfmt "${PREBUILT_RUST_TOOLS_DIR}/bin/rustfmt"

    # TODO(fxbug.dev/45007): Syzkaller does not support enum member references in struct
    # defaults.
    if [ "struct_default_value_enum_library_reference.test" = "${src_name}" ]; then
        continue
    fi
    GOLDENS+=(
      "${syzkaller_name}.golden",
    )
    echo "  syzkaller: ${json_name} > ${syzkaller_name}"
    ${FIDLGEN_SYZ} \
        -json "${GOLDENS_DIR}/${json_name}" \
        -output-syz "${GOLDENS_DIR}/${syzkaller_name}.golden"
done

> "${GOLDENS_DIR}/goldens.txt"
printf "%s\n" "${GOLDENS[@]//,}" | sort >> "${GOLDENS_DIR}/goldens.txt"

> "${GOLDENS_DIR}/OWNERS"
find "${EXAMPLE_DIR}/../.." \
    "tools/fidl/fidlgen_"{go,hlcpp,rust,libfuzzer,syzkaller} \
    -name 'OWNERS' -exec cat {} \; \
    | grep -v -e "^#" | awk 'NF' | sort -u > "${GOLDENS_DIR}/OWNERS"
echo "" >> "${GOLDENS_DIR}/OWNERS"
echo "# COMPONENT: FIDL>Testing" >> "${GOLDENS_DIR}/OWNERS"
