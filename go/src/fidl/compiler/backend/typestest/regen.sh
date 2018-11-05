#!/usr/bin/env bash

EXAMPLE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="$( echo ${EXAMPLE_DIR} | sed -e 's,garnet/go/src.*$,,' )"

FIDLC=${FUCHSIA_DIR}out/x64/host_x64/fidlc
FIDLGEN=${FUCHSIA_DIR}out/x64/host_x64/fidlgen

if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you fx clean-build x64?" 1>&2
    exit 1
fi

if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen missing; maybe fx clean-build x64?" 1>&2
    exit 1
fi

for src_path in `find "${EXAMPLE_DIR}" -name '*.fidl'`; do
    src_name="$( basename "${src_path}" )"
    json_name=${src_name}.json
    cpp_header_name=${json_name}.h
    cpp_test_header_name=${json_name}_test_base.h
    cpp_source_name=${json_name}.cc
    go_impl_name=${json_name}.go
    rust_name=${json_name}.rs

    echo -e "\033[1mexample: ${src_name}\033[0m"

    echo "  json ir: ${src_name} > ${json_name}"
    ${FIDLC} \
        --json "${EXAMPLE_DIR}/${json_name}" \
        --files "${EXAMPLE_DIR}/${src_name}"

    echo "  cpp: ${json_name} > ${cpp_header_name}, ${cpp_source_name}, and ${cpp_test_header_name}"
    ${FIDLGEN} \
        -generators cpp \
        -json "${EXAMPLE_DIR}/${json_name}" \
        -output-base "${EXAMPLE_DIR}/${json_name}" \
        -include-base "${EXAMPLE_DIR}"
    mv "${EXAMPLE_DIR}/${cpp_header_name}" "${EXAMPLE_DIR}/${cpp_header_name}.golden"
    mv "${EXAMPLE_DIR}/${cpp_source_name}" "${EXAMPLE_DIR}/${cpp_source_name}.golden"
    mv "${EXAMPLE_DIR}/${cpp_test_header_name}" "${EXAMPLE_DIR}/${cpp_test_header_name}.golden"

    echo "  go: ${json_name} > ${go_impl_name}"
    ${FIDLGEN} \
        -generators go \
        -json "${EXAMPLE_DIR}/${json_name}" \
        -output-base "${EXAMPLE_DIR}" \
        -include-base "${EXAMPLE_DIR}"
    rm "${EXAMPLE_DIR}/pkg_name"
    mv "${EXAMPLE_DIR}/impl.go" "${EXAMPLE_DIR}/${go_impl_name}.golden"

    echo "  rust: ${json_name} > ${rust_name}"
    ${FIDLGEN} \
        -generators rust \
        -json "${EXAMPLE_DIR}/${json_name}" \
        -output-base "${EXAMPLE_DIR}/${json_name}" \
        -include-base "${EXAMPLE_DIR}"
    mv "${EXAMPLE_DIR}/${rust_name}" "${EXAMPLE_DIR}/${rust_name}.golden"
done
