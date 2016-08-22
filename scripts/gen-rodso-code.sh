#!/usr/bin/env bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# This script reads symbols with nm and writes a C header file that
# defines macros <NAME>_CODE_START, <NAME>_CODE_END, and <NAME>_ENTRY,
# with the address constants found in the symbol table for the symbols
# CODE_START, CODE_END, and _start, respectively.

usage() {
  echo >&2 "Usage: $0 NM {NAME DSO}..."
  exit 2
}

if [ $# -lt 3 ]; then
  usage
fi

NM="$1"
shift

set -o pipefail -e

find_code_symbol() {
  local status=1
  local symbol type addr rest
  while read symbol type addr rest; do
    case "$symbol" in
    CODE_START|CODE_END|_start)
      if [ "$symbol" = _start ]; then
        symbol=ENTRY
      fi
      echo "#define ${1}_${symbol} 0x${addr}"
      status=0
      ;;
    esac
  done
  return $status
}

while [ $# -gt 0 ]; do
  if [ $# -lt 2 ]; then
    usage
  fi
  "$NM" -P "$2" | find_code_symbol "$1"
  shift 2
done
