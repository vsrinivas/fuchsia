#!/usr/bin/env bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# This script reads symbols with nm and writes a C header file that
# defines macros <NAME>_CODE_*, <NAME>_DATA_* and <NAME>_ENTRY, with
# the address constants found in the symbol table for the symbols
# CODE_*, DATA_* and _start, respectively.
#
# When there is a dynamic symbol table, then it also emits macros
# <NAME>_DYNSYM_* giving the dynamic symbol table index of each
# exported symbol, and <NAME>_DYNSYM_COUNT giving the total number
# of entries in the table.

usage() {
  echo >&2 "Usage: $0 NM OUTFILE {NAME DSO}..."
  exit 2
}

if [ $# -lt 3 ]; then
  usage
fi

NM="$1"
shift
OUTFILE="$1"
shift

if [ "$(basename $0)" = "sh" ]
then
  set -e
else
  set -o pipefail -e
fi

grok_code_symbols() {
  local symbol type addr size rest
  while read symbol type addr size rest; do
    case "$symbol" in
    CODE_*|DATA_*|SYSCALL_*|_start)
      if [ "$symbol" = _start ]; then
        symbol=ENTRY
      fi
      echo "#define ${1}_${symbol} 0x${addr}" >> $OUTFILE
      case "$size" in
      ''|0|0x0) ;;
      *) echo "#define ${1}_${symbol}_SIZE 0x${size}" >> $OUTFILE
      esac
      status=0
      ;;
    esac
  done
  return $status
}

find_code_symbols() {
  "$NM" -P -S -n "$2" | grok_code_symbols "$1"
}

grok_dynsym_slots() {
  local symno=0
  local symbol rest
  while read symbol rest; do
    symno=$((symno+1))
    echo "#define ${1}_DYNSYM_${symbol} ${symno}" >> $OUTFILE
  done
  if [ $symno -gt 0 ]; then
    symno=$((symno+1))
    echo "#define ${1}_DYNSYM_COUNT ${symno}" >> $OUTFILE
  fi
}

find_dynsym_slots() {
  "$NM" -P -D -p "$2" | grok_dynsym_slots "$1"
}

while [ $# -gt 0 ]; do
  if [ $# -lt 2 ]; then
    usage
  fi
  echo "#define ${1}_FILENAME \"${2}\"" > $OUTFILE
  find_code_symbols "$1" "$2"
  find_dynsym_slots "$1" "$2"
  shift 2
done
