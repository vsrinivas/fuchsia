#!/usr/bin/env bash
set -eufo pipefail

if [ ! -x "${FUCHSIA_BUILD_DIR}" ]; then
    echo "error: did you fx exec? missing \$FUCHSIA_BUILD_DIR" 1>&2
    exit 1
fi

FIDLC_TARGET=$( fx list-build-artifacts --expect-one --name fidlc tools )

FIDLC="${FUCHSIA_BUILD_DIR}/${FIDLC_TARGET}"
if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you `fx build ${FIDLC_TARGET}`?" 1>&2
    exit 1
fi

EXAMPLE_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
EXAMPLE_DIR=$( echo "$EXAMPLE_DIR" | sed -e "s+${FUCHSIA_DIR}/++" )

# Unlike in fidlgen, this regen script runs fidlc from within the same directory
# as each fidl file it regens. This makes it simpler to match the "filename" field
# inside the JSON goldens when running json_generator_tests.cc

cd "${FUCHSIA_DIR}/${EXAMPLE_DIR}"
while read -r src_path; do
    src_name="$( basename "${src_path}" )"
    json_name=$( echo "${src_name}" | cut -f 1 -d '.').test.json.golden
    coding_tables_name=$( echo "${src_name}" | cut -f 1 -d '.').test.tables.c.golden

    JSONIR_GOLDENS+=("${json_name}")

    echo -e "\033[1mexample: ${src_name}\033[0m"
    echo "  json ir: ${src_name} > ${json_name}"
    echo "  coding table: ${src_name} > ${coding_tables_name}"
    ${FIDLC} \
        --json "../goldens/${json_name}" \
        --tables "../goldens/${coding_tables_name}" \
        --experimental enable_handle_rights \
        --files "../../../vdso/zx_common.fidl" \
        --files "${src_name}"
done < <(find . -maxdepth 1 -name '*.fidl')

cd "${FUCHSIA_DIR}"
while read -r lib_path; do
    lib_name="$( basename "${lib_path}" )"
    json_name=${lib_name}.test.json.golden
    coding_tables_name=$( echo "${lib_name}" | cut -f 1 -d '.').test.tables.c.golden

    JSONIR_GOLDENS+=("${json_name}")

    echo -e "\033[1mexample: ${lib_name}\033[0m"
    echo "  json ir: ${lib_name} > ${json_name}"
    echo "  coding table: ${lib_name} > ${coding_tables_name}"
    cd "${FUCHSIA_DIR}/${lib_path}"
    ${FIDLC} \
        --json "../../goldens/${json_name}" \
        --tables "../../goldens/${coding_tables_name}" \
        --experimental enable_handle_rights \
        --files "../../../../vdso/zx_common.fidl" \
        $( awk '{print "--files " $0}' < order.txt | tr '\n' ' ' )
done < <(find "${EXAMPLE_DIR}" -maxdepth 1 ! -path "${EXAMPLE_DIR}" -type d)

> "../../goldens/jsonir_goldens.txt"
printf "%s\n" "${JSONIR_GOLDENS[@]//,}" | sort > "../../goldens/jsonir_goldens.txt"
