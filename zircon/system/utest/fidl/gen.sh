#!/usr/bin/env bash

set -eu
set -o pipefail

TEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="$( echo ${TEST_DIR} | sed -e 's,zircon/system/utest.*$,,' )"

if [ -z ${FUCHSIA_BUILD_DIR+x} ] || [ -z ${ZIRCON_TOOLS_DIR+x} ]; then
    echo "please use fx exec to run this script" 1>&2
    exit 1
fi

FIDLC=${ZIRCON_TOOLS_DIR}/fidlc
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

${FIDLC} \
  --tables ${TEST_DIR}/generated/extra_messages.c \
  --files ${TEST_DIR}/extra_messages.test.fidl

${FIDLC} \
  --tables ${TEST_DIR}/generated/transformer_tables.test.h \
  --files ${TEST_DIR}/transformer.test.fidl

for src_path in llcpp.test.fidl; do
  src_name="$( basename "${src_path}" .fidl )"
  json_name=${src_name}.json

  # generate the json IR
  cd ${TEST_DIR}
  ${FIDLC} --json /tmp/${json_name} \
           --tables generated/fidl_llcpp_tables_${src_name}.c \
           --files ${src_path}

  # generate llcpp bindings
  ${FIDLGEN} -json /tmp/${json_name} \
             -header fidl_llcpp_${src_name}.h \
             -source fidl_llcpp_${src_name}.cc \
             -include-base .

  # generate tables in c
  ${FIDLC} \
    --tables ${TEST_DIR}/generated/extra_messages.c \
    --files ${TEST_DIR}/extra_messages.test.fidl

  mv fidl_llcpp_${src_name}.h generated/fidl_llcpp_${src_name}.h
  mv fidl_llcpp_${src_name}.cc generated/fidl_llcpp_${src_name}.cc

  # cleanup
  rm /tmp/${json_name}
done
