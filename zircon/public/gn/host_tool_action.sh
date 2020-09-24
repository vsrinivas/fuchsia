#!/bin/bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used by host_tool_action.gni, which see.
#   Usage: host_tool_action.sh RSPFILE OUTPUT DEPFILE ORIG_DEPFILE ARG...

# This runs for an action() whose first $outputs file is `"OUTPUT"` and that
# uses `depfile = "DEPFILE"`.  DEPFILE will be written with dependencies showing
# OUTPUT as the target.  RSPFILE contains input files (to be recorded in the
# DEPFILE) and command line words (command name/path and arguments).  These
# arguments are prepended to the ARG... list.  Then all `@rspfile` words in the
# list are expanded from the file contents to form the final command line.  If
# this command succeeds, DEPFILE will be written out.  If ORIG_DEPFILE is not -,
# then it's the name of the depfile that the command itself will have written.
# In that case, it's presumed it's correctly formatted with OUTPUT as its
# target, and additional input files (RSPFILE et al) are appended.

set -e

RSPFILE="$1"
OUTPUT="$2"
DEPFILE="$3"
ORIG_DEPFILE="$4"
shift 4

CMD=()
FILES=()

# The $RSPFILE file contains:
#  * one input file name per line
#  * a line of just `--`
#  * one command/argument word per line
read_rspfile() {
  local word
  while read word; do
    if [ "$word" = -- ]; then
      break
    else
      FILES+=("$word")
    fi
  done
  while read word; do
    CMD+=("$word")
  done
}

process_args() {
  local arg rspfile
  while [ $# -gt 0 ]; do
    arg="$1"
    shift
    # An `@rspfile` argument reads `rspfile` for more arguments.
    # This counts `rspfile` as a dependency input.
    # `rspfile` cannot itself contain `@otherrspfile` lines.
    if [[ "$arg" == @* ]]; then
      rspfile="${arg#@}"
      FILES+=("$rspfile")
      while read arg; do
        CMD+=("$arg")
      done < "$rspfile"
    else
      CMD+=("$arg")
    fi
  done
}

read_rspfile < "$RSPFILE"
process_args "$@"

# Ninja created directories for the output file, but not for the depfile.
OUTPUT_DIR="$(dirname "$OUTPUT")"
DEPFILE_DIR="$(dirname "$DEPFILE")"
if [ "$DEPFILE_DIR" != "$OUTPUT_DIR" ]; then
  mkdir -p "$DEPFILE_DIR"
fi
if [ "$ORIG_DEPFILE" != - ]; then
  ORIG_DEPFILE_DIR="$(dirname "$ORIG_DEPFILE")"
  if [ "$ORIG_DEPFILE_DIR" != "$DEPFILE_DIR" ] &&
     [ "$ORIG_DEPFILE_DIR" != "$OUTPUT_DIR" ]; then
    mkdir -p "$ORIG_DEPFILE_DIR"
  fi
fi

"${CMD[@]}"

if [ "$ORIG_DEPFILE" = - ]; then
  # The host_tool_action() has no user-specified depfile, so just generate
  # one that lists the files from the metadata rspfile.
  echo "$OUTPUT: ${FILES[*]}" > "$DEPFILE"
else
  # Append the file list to the user-generated depfile.
  echo "$(< "$ORIG_DEPFILE") ${FILES[*]}" > "$DEPFILE"
fi
