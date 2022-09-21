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
  echo >&2 "\
Usage: $0 NM READELF [--depfile DEPFILE] [--writable] OUTFILE {NAME DSO}..."
  exit 2
}

if [ $# -lt 3 ]; then
  usage
fi

NM="$1"
shift
READELF="$1"
shift

DEPFILE=
if [ $# -gt 2 -a "$1" = "--depfile" ]; then
  DEPFILE="$2"
  shift 2
fi
USED_FILES=()

OUTFILE="$1"
shift

set -e
if [ -n "$BASH_VERSION" ]; then
  set -o pipefail
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

SEGMENTS_HAVE_DYNSYM=22
SEGMENTS_NO_DYNSYM=33

grok_segments() {
  local line
  local status=$SEGMENTS_NO_DYNSYM
  while read line; do
    case "$line" in
    # Program header for the code segment, e.g.:
    # LOAD           0x002000 0x0000000000002000 0x0000000000002000 0x00f50f 0x00f50f R? E 0x1000
    #                         ^^^^^ vaddr ^^^^^^                    ^filesz^          ^ not present on aarch64
    *LOAD*\ E\ *)
      local words=($line)
      local vaddr=$(printf '%#x' ${words[2]})
      local filesz=$(printf '%#x' ${words[4]})
      echo "#define ${1}_CODE_START $vaddr" >> $OUTFILE
      echo "#define ${1}_CODE_END (((${1}_CODE_START + $filesz + (1 << PAGE_SIZE_SHIFT) - 1) >> PAGE_SIZE_SHIFT) << PAGE_SIZE_SHIFT)" >> $OUTFILE
      ;;
    # Make sure there's no writable segment unless --writable allows it.
    *LOAD*W*)
      $WRITABLE || {
        echo >&2 "$0: writable segment: $line"
        exit 1
      }
      echo "#define ${1}_DATA_START $vaddr" >> $OUTFILE
      echo "#define ${1}_DATA_END (((${1}_DATA_START + $filesz + (1 << PAGE_SIZE_SHIFT) - 1) >> PAGE_SIZE_SHIFT) << PAGE_SIZE_SHIFT)" >> $OUTFILE
      ;;
    # Section header for .dynsym, e.g.:
    #  [ 3] .dynsym           DYNSYM          0000000000001268 001268 000018 18   A  6   1  8
    #                                         ^^^^^ addr ^^^^^        ^size^ ^entsize^
    *DYNSYM*)
      line="${line#*DYNSYM}"
      local words=($line)
      local addr=$(printf '%#x' 0x${words[0]})
      local size=$(printf '%#x' 0x${words[2]})
      local entsize=$(printf '%#x' 0x${words[3]})
      # A stub .dynsym has a single entry.  Don't count that case.
      if [ $size != $entsize ]; then
        status=$SEGMENTS_HAVE_DYNSYM
        echo "#define ${1}_DATA_START_dynsym $addr" >> $OUTFILE
        echo "#define ${1}_DATA_END_dynsym (${1}_DATA_START_dynsym + $size)" >> $OUTFILE
      fi
      ;;
    # Hex dump of section '.note.gnu.build-id':
    # 0x000001c8 04000000 08000000 03000000 474e5500 ............GNU.
    *Hex\ dump*)
      # This always comes last, so we can consume the rest of the lines.
      grok_build_id "$1"
      ;;
    esac
  done
  return $status
}

grok_build_id() {
  # The first line looks like:
  #   0x000001c8 04000000 08000000 03000000 474e5500 ............GNU.
  # where 0x000001c8 is the address of the section.  Subsequent lines look
  # similar, but possibly with an odd ending:
  #   0x000001d8 9470536b 2dd1e5e7                   .pSk-...
  local -a hex words=()
  read -a hex
  local addr="${hex[0]}"
  local hexp='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'

  while true; do
    local n=${#hex[@]} i=1
    # The "...text..." portion can contain spaces, so the word count
    # is not a reliable way to detect the last hex word.  So also stop
    # when the first non-hex word is seen.
    while ((i < n - 1)) && [[ "${hex[$i]}" == $hexp ]]; do
      words+=("${hex[$i]}")
      ((i++))
    done
    read -a hex || break
  done

  local -a bytes=()
  local w
  for w in "${words[@]}"; do
    bytes+=("0x${w:0:2}," "0x${w:2:2}," "0x${w:4:2}," "0x${w:6:2},")
  done

  echo "#define ${1}_BUILD_ID_NOTE_ADDRESS $addr" >> "$OUTFILE"
  echo "#define ${1}_BUILD_ID_NOTE_SIZE ${#bytes[@]}" >> "$OUTFILE"
  echo "#define ${1}_BUILD_ID_NOTE_BYTES {${bytes[@]}}" >> "$OUTFILE"
}

find_segments() {
  # This if silliness disarms -e so we can observe grok_segments's return code.
  if {
  "$READELF" -W -S -l -x.note.gnu.build-id "$2" | grok_segments "$1"
  case $? in
  $SEGMENTS_NO_DYNSYM) have_dynsym=no ;;
  $SEGMENTS_HAVE_DYNSYM) have_dynsym=yes ;;
  *) exit $? ;;
  esac
  return 0
  }; then : ; fi
}

trap 'rm -f "$OUTFILE" ${DEPFILE:+"$DEPFILE"}' ERR HUP INT TERM

while [ $# -gt 0 ]; do
  if [ "$1" = --writable ]; then
    WRITABLE=true
    shift
  else
    WRITABLE=false
  fi

  if [ $# -lt 2 ]; then
    usage
  fi
  name="$1"
  dso="$2"
  shift 2

  # This argument might be a response file.
  if [[ "$dso" == @* ]]; then
    rspfile="${dso#@}"
    dso="$(< "$rspfile")"
    USED_FILES+=("$rspfile" "$dso")
  else
    USED_FILES+=("$dso")
  fi

  echo "#define ${name}_FILENAME \"${dso}\"" > $OUTFILE
  find_segments "$name" "$dso"
  find_code_symbols "$name" "$dso"
  if [ $have_dynsym = yes ]; then
    find_dynsym_slots "$name" "$dso"
  fi
done

if [ -n "$DEPFILE" ]; then
  echo "$OUTFILE: ${USED_FILES[*]}" > "$DEPFILE"
fi
