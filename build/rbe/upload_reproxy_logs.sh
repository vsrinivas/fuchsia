#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Uploads reproxy logs and metrics to Fuchsia's BQ tables.

# usage:
# $0 [options] reproxy-log-dirs...

# Most defaults are already set in the .py script.
# Example: to manually upload a batch of existing log dirs:
# $0 /tmp/reproxy_*

script="$0"
script_dir="$(dirname "$script")"
script_dir_abs="$(readlink -f "$script_dir")"
project_root="$(readlink -f "$script_dir"/../..)"

python=( "$project_root"/prebuilt/third_party/python3/*/bin/python3 )

test -f "$script_dir"/proto/api/proxy/log_pb2.py || {
  cat <<EOF
Generated source $script_dir/proto/api/proxy/log_pb2.py not found.
Run $script_dir/proto/refresh.sh first.
EOF
  exit 1
}

env \
  PYTHONPATH="$script_dir_abs":"$script_dir_abs"/proto:"$project_root"/third_party/protobuf/python \
  "${python[0]}" \
  -S \
  "$script_dir"/upload_reproxy_logs.py \
  "$@"
