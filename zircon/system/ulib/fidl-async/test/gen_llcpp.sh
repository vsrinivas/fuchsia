#!/usr/bin/env bash

set -eu
set -o pipefail

LLCPP_TEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="$( echo ${LLCPP_TEST_DIR} | sed -e 's,zircon/system/utest.*$,,' )"

if [ -z ${FUCHSIA_BUILD_DIR+x} ]; then
    echo "please use fx exec to run this script" 1>&2
    exit 1
fi

FIDLC=${FUCHSIA_BUILD_DIR}/host_x64/fidlc
FIDLGEN=${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_llcpp

if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you fx clean-build?" 1>&2
    exit 1
fi

if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen_llcpp missing; did you fx clean-build?" 1>&2
    exit 1
fi

cd ${FUCHSIA_DIR}

for src_path in `find "${LLCPP_TEST_DIR}" -name '*.fidl'`; do
  src_name="$( basename "${src_path}" .fidl )"
  json_name=${src_name}.json

  # generate the json IR
  cd ${LLCPP_TEST_DIR}
  ${FIDLC} --json /tmp/${json_name} \
           --tables generated/fidl_llcpp_tables_${src_name}.c \
           --files ${src_path}

  # generate llcpp bindings
  ${FIDLGEN} -json /tmp/${json_name} \
             -header fidl_llcpp_${src_name}.h \
             -source fidl_llcpp_${src_name}.cc \
             -include-base .

  # move bindings to the `generated` directory
  mv fidl_llcpp_${src_name}.h ${LLCPP_TEST_DIR}/generated
  mv fidl_llcpp_${src_name}.cc ${LLCPP_TEST_DIR}/generated

  # cleanup
  rm /tmp/${json_name}
done
