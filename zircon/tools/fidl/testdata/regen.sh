#!/usr/bin/env bash
set -eufo pipefail

if [ ! -x "${FUCHSIA_BUILD_DIR}" ]; then
    echo "error: did you fx exec? missing \$FUCHSIA_BUILD_DIR" 1>&2
    exit 1
fi

FIDLC="${FUCHSIA_BUILD_DIR}/../default.zircon/host-x64-linux-asan/obj/tools/fidl/fidlc"
if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you fx clean-build?" 1>&2
    exit 1
fi

EXAMPLE_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
EXAMPLE_DIR=$( echo "$EXAMPLE_DIR" | sed -e "s+${FUCHSIA_DIR}/++" )

# Unlike in fidlgen, this regen script runs fidlc from within the same directory
# as each fidl file it regens. This makes it simpler to match the "filename" field
# inside the JSON goldens when running json_generator_tests.cc

cd "${EXAMPLE_DIR}"
while read -r src_path; do
    src_name="$( basename "${src_path}" )"
    json_name=$( echo "${src_name}" | cut -f 1 -d '.').test.json.golden

    echo -e "\033[1mexample: ${src_name}\033[0m"
    echo "  json ir: ${src_name} > ${json_name}"
    ${FIDLC} \
        --json "../goldens/${json_name}" \
        --experimental enable_handle_rights \
        --files "${src_name}"
done < <(find . -maxdepth 1 -name '*.fidl')

cd "${FUCHSIA_DIR}"
while read -r lib_path; do
    lib_name="$( basename "${lib_path}" )"
    json_name=${lib_name}.test.json.golden

    echo -e "\033[1mexample: ${lib_name}\033[0m"
    echo "  json ir: ${lib_name} > ${json_name}"
    cd "${FUCHSIA_DIR}/${lib_path}"
    ${FIDLC} \
        --json "../../goldens/${json_name}" \
        --experimental enable_handle_rights \
        $( awk '{print "--files " $0}' < order.txt | tr '\n' ' ' )
done < <(find "${EXAMPLE_DIR}" -maxdepth 1 ! -path "${EXAMPLE_DIR}" -type d)
cd "${FUCHSIA_DIR}"
