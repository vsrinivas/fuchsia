#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
source "$(cd "${script_dir}/../../../../../tools/devshell" && pwd)"/lib/vars.sh || exit $?
fx-config-read

case "$1" in
intel)
  fx-command-run test --no-build --simple --exact -o \
    magma_nonhardware_tests \
    msd_intel_gen_nonhardware_tests \
    mesa_unit_tests \
    magma-conformance-tests \
    msd_intel_gen_integration_tests \
    vkreadback_test \
    vkloop \
    vkext_unprotected \
    mesa_integration_tests
  ;;
mali)
  fx-command-run test --no-build --simple -o \
    magma_nonhardware_tests \
    msd_arm_mali_nonhardware_tests \
    magma-conformance-tests-arm \
    vkreadback_test \
    vkloop \
    vkext_protected \
    vkext_unprotected \
    vk_timer_query \
    msd-arm-mali-integration-tests
  ;;
vsi)
  fx-command-run test --simple -o \
    magma_nonhardware_tests \
    msd-vsi-vip-nonhardware-tests \
    magma-conformance-tests-vsi \
    msd-vsi-vip-integration-tests \
    test-magma-vsi-exec \
  ;;
*)
  echo >&2 "unknown gpu: $1"
  echo >&2 "usage: $0 [intel|mali|vsi]"
  exit 1
  ;;
esac
