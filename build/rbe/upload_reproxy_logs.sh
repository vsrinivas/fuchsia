#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script="$0"
script_dir="$(dirname "$script")"
script_dir_abs="$(readlink -f "$script_dir")"
project_root="$(readlink -f "$script_dir"/../..)"

python="$project_root"/prebuilt/third_party/python3/linux-x64/bin/python3

env \
  PYTHONPATH="$script_dir_abs":"$script_dir_abs"/proto:"$project_root"/third_party/protobuf/python \
  "$python" \
  -S \
  "$script_dir"/upload_reproxy_logs.py \
  "$@"
