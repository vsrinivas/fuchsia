#!/usr/bin/env bash

set -eu
set -o pipefail

LLCPP_TEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="$( echo ${LLCPP_TEST_DIR} | sed -e 's,zircon/system/utest.*$,,' )"

FIDLC=${FUCHSIA_DIR}out/default/host_x64/fidlc
FIDLGEN=${FUCHSIA_DIR}out/default/host_x64/fidlgen

if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you fx clean-build x64?" 1>&2
    exit 1
fi

if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen missing; did you fx clean-build x64?" 1>&2
    exit 1
fi

cd ${FUCHSIA_DIR}

for src_path in `find "${LLCPP_TEST_DIR}" -name '*.fidl'`; do
  src_name="$( basename "${src_path}" .fidl )"
  json_name=${src_name}.json

  # generate the json IR
  cd ${LLCPP_TEST_DIR}
  ${FIDLC} --json /tmp/${json_name} \
           --files ${src_path}

  # generate llcpp bindings
  ${FIDLGEN} -generators llcpp \
             -json /tmp/${json_name} \
             -output-base fidl_llcpp_${src_name} \
             -include-base .

  # move bindings to the `generated` directory
  mv fidl_llcpp_${src_name}.h ${LLCPP_TEST_DIR}/generated
  mv fidl_llcpp_${src_name}.cc ${LLCPP_TEST_DIR}/generated

  # cleanup
  rm /tmp/${json_name}
done
