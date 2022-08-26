#!/bin/bash -e
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# See usage() for description.
#
# TODO(fangism): git clone re-client to a tempdir if repo doesn't already exist

script="$0"
script_dir="$(dirname "$script")"
project_root="$(readlink -f "$script_dir"/../../..)"

function usage() {
  cat <<EOF
Usage: $0 reclient-srcdir

This script updates the public protos needed for reproxy logs collection.

Args: location of reclient source
      from git clone sso://team/foundry-x/re-client

This populates the Fuchsia source tree with the following files:

  build/rbe/proto/api/proxy/log.proto
  build/rbe/proto/api/proxy/log_pb2.py
  build/rbe/proto/api/stats/stats.proto
  build/rbe/proto/api/stats/stats_pb2.py
  build/rbe/proto/go/api/command/command.proto
  build/rbe/proto/go/api/command/command_pb2.py
  third_party/protobuf/python/timestamp_pb2.py
  third_party/protobuf/python/descriptor_pb2.py
EOF
}

test "$#" = 1 || {
  usage
  exit 1
}

RECLIENT_SRCDIR="$1"
DESTDIR="$script_dir"

echo "Fetching protos from $RECLIENT_SRCDIR, installing to $DESTDIR"
mkdir -p "$DESTDIR"/api/proxy
grep -v "bq_table.proto" "$RECLIENT_SRCDIR"/api/proxy/log.proto | \
  grep -v "option.*gen_bq_schema" > "$DESTDIR"/api/proxy/log.proto
mkdir -p "$DESTDIR"/api/stats
cp "$RECLIENT_SRCDIR"/api/stats/stats.proto "$DESTDIR"/api/stats/
mkdir -p "$DESTDIR"/go/api/command

echo "Fetching proto from http://github.com/bazelbuild/remote-apis-sdks"
curl https://raw.githubusercontent.com/bazelbuild/remote-apis-sdks/master/go/api/command/command.proto > "$DESTDIR"/go/api/command/command.proto

cd "$project_root"
protoc=(fx host-tool protoc)
# TODO(fangism): provide prebuilt
# Caveat: if fx build-metrics is already enabled with RBE, this fx build may
# attempt to process and upload metrics before it is ready, and fail.

readonly PROTOBUF_SRC=third_party/protobuf/src
readonly PROTOBUF_DEST=third_party/protobuf/python

echo "Compiling $DESTDIR protos to Python"
LOG_PROTOS=(
  api/proxy/log.proto
  api/stats/stats.proto
  go/api/command/command.proto
)
for proto in "${LOG_PROTOS[@]}"
do
  "${protoc[@]}" \
    -I="$DESTDIR" \
    -I="$PROTOBUF_SRC" \
    --python_out="$DESTDIR" \
    "$DESTDIR"/"$proto"
done

# Generate third_party/protobuf/src -> third_party/protobuf/python.
# NOTE: These generated *_pb2.py are NOT checked-in.
# TODO(fangism): provide prebuilt package with protos already compiled.
# Placing these in "$DESTDIR" doesn't work, because imports get confused with
# the path to one of two locations with 'google.protobuf'.
echo "Compiling $PROTOBUF_SRC protos to Python in $PROTOBUF_DEST"
PB_PROTOS=(
  timestamp.proto
  descriptor.proto
)
for proto in "${PB_PROTOS[@]}"
do
  "${protoc[@]}" \
    -I="$PROTOBUF_SRC" \
    -I="$PROTOBUF_DEST" \
    --python_out="$PROTOBUF_DEST" \
    "$PROTOBUF_SRC"/google/protobuf/"$proto"
done

echo "Done."

