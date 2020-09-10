#!/bin/bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e -o pipefail

usage() {
  echo >&2 "Usage: $0 READELF BINARY STAMPFILE DEPFILE"
  echo >&2 "  Note: BINARY can be @RSPFILE"
  exit 2
}

if [ $# -ne 4 ]; then
  usage
fi

readonly READELF="$1"
BINARY="$2"
readonly STAMPFILE="$3"
readonly DEPFILE="$4"

# Expand @RSPFILE syntax.
case "$BINARY" in
  @*) BINARY="$(<"${BINARY#@}")" ;;
esac
readonly BINARY

check() {
  local soname='' init_arraysz=''

  local tag type value rest
  while read tag type value rest; do
    case "$type" in
      '(SONAME)') soname="$value" ;;
      '(INIT_ARRAYSZ)') init_arraysz="$value" ;;
    esac
  done

  if [ -z "$soname" ]; then
    echo >&2 "*** Failed to find SONAME in $BINARY"
    exit 1
  fi

  if [ -n "$init_arraysz" ]; then
    if ((init_arraysz % 8 != 0)); then
      echo >&2 "*** confused by INIT_ARRAYSZ value $init_arraysz"
      exit 1
    fi
    ((init_arraysz /= 8))
    if ((init_arraysz != 0)); then
      echo >&2 "*** at least $init_arraysz static constructors found in $BINARY"
      exit 1
    fi
  fi
}

rm -f "$STAMPFILE"

"$READELF" -W -d "$BINARY" | check

echo "$STAMPFILE: $READELF $BINARY" > "$DEPFILE"
echo OK > "$STAMPFILE"
