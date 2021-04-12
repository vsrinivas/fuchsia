#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly dir="$1"
shift 1
readonly tool="$1"
shift 1

cd "${dir}"
"${tool}" $*
