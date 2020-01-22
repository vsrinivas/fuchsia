#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
source "$(cd "${script_dir}/../../../../tools/devshell" && pwd)"/lib/vars.sh || exit $?
fx-config-read

case "$1" in
intel)
  fx-command-run cp "${script_dir}/autorun_intel" /tmp/magma_autorun
  ;;
mali)
  fx-command-run cp "${script_dir}/autorun_mali" /tmp/magma_autorun
  ;;
pvr)
  fx-command-run cp "${script_dir}/autorun_pvr" /tmp/magma_autorun
  ;;
*)
  echo >&2 "unknown gpu: $1"
  echo >&2 "usage: $0 [intel|mali|pvr]"
  exit 1
  ;;
esac

fx-command-run shell -t runtests -f /tmp/magma_autorun
