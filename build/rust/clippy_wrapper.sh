#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function usage() {
  cat <<EOF
$0 [options] -- clippy-driver-arguments...

Options:
  --help | -h : print help and exit
  --output FILE : clippy file to output (required)
  --jq FILE : path to 'jq' (required)
  --fail : clippy cause failure

EOF
}

output=
jq=
driver_options=()
fail=0

# Extract options before --
prev_opt=
for opt
do
  # handle --option arg
  if test -n "$prev_opt"
  then
    eval "$prev_opt"=\$opt
    prev_opt=
    shift
    continue
  fi
  # Extract optarg from --opt=optarg
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac
  case "$opt" in
    --help|-h) usage ; exit ;;
    --output) prev_opt=output ;;
    --output=*) output="$optarg" ;;
    --jq) prev_opt=jq ;;
    --jq=*) jq="$optarg" ;;
    --fail) fail=1 ;;
    # stop option processing
    --) shift; break ;;
    # Forward all other options to clippy-driver
    *) driver_options+=( "$opt" ) ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

test -n "$output" || { echo "Error: --output required, but missing." ; exit 1 ;}
test -n "$jq" || { echo "Error: --jq required, but missing." ; exit 1 ;}

# After -- the remaining args are the clippy-driver command and args set
# in the clippy GN template.
filtered_driver_options=()
for opt in "${driver_options[@]}" "$@"
do case "$opt" in
  --remote* ) ;;  # pseudoflag for RBE parameters, drop it
  *) filtered_driver_options+=( "$opt" )
esac
done

deps=( $(<"$output.deps") )
transdeps=( $(sort -u "$output.transdeps") )

command=(
  "${filtered_driver_options[@]}"
  -Cpanic=abort
  -Zpanic_abort_tests
  -Zno_codegen
  "${deps[@]}"
  "${transdeps[@]}"
  --emit metadata="$output.rmeta"
  --error-format=json
  --json=diagnostic-rendered-ansi
)

RUSTC_LOG=error "${command[@]}" 2>"$output"
result="$?"

if [[ "$result" != 0 && "$fail" = 1 ]]; then
    "$jq" -sre '.[] | select(.level == "error") | .rendered' "$output" || cat "$output" >&2
    exit "$result"
fi
