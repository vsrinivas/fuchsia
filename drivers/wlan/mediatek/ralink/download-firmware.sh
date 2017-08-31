#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_URL_BASE="https://storage.googleapis.com/fuchsia-build"
readonly FUCHSIA_ROOT="${SCRIPT_ROOT}/../../../../"
. "${FUCHSIA_ROOT}/buildtools/download.sh"

download_zipfile rt2870 "${FUCHSIA_URL_BASE}/firmware/ralink" "${SCRIPT_ROOT}/firmware"
