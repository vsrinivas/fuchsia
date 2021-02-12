#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

script="$(basename "$0")"

function usage() {
  cat <<EOF
$script
Generates dynamic depedencies (dyndep) file for an archive that can be
used for Ninja builds.

See https://ninja-build.org/manual.html#_tarball_extraction
for an explanation.
This script implements the "scantar" tool mentioned there.

Usage:
  $script --stamp STAMP ARCHIVE > output.tar.dd

ninja.build:
  rule untargz
    command = tar xzf \$in && touch \$out
  rule scantar
    command = $script --stamp \$stamp \$in > \$out
  build foo.tar.dd: scantar foo.tar.gz
    stamp = foo.tar.stamp
  build foo.tar.stamp: untargz foo.tar.gz || foo.tar.dd
    dyndep = foo.tar.dd

Supported archive extensions: .tar .tar.gz .tgz .tar.bz2

EOF
}

# Conventional option processing loop.
stamp=""
args=()
for opt
do
  # handle: --option arg
  if test -n "$prev_opt"
  then
    eval $prev_opt=\$opt
    prev_opt=
    shift
    continue
  fi
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac
  case $opt in
    -- ) shift ; break ;; # stop option processing
    --help | -h ) { usage ; exit ;} ;;
    --stamp ) prev_opt=stamp ;;
    --stamp=* ) stamp="$optarg" ;;
    --* | -* ) msg "Unknown option: $opt" ; exit 1 ;;
    # Append positional arguments.
    *) args=("${args[@]}" "$opt")
  esac
  shift
done

# Collect positional arguments.
set -- "${args[@]}" "$@"
test "$#" = 1 || {
  echo "Expected exactly one archive argument, but got: $@"
  exit 1
}
archive="$1"

test -n "$stamp" || {
  echo "Required, but missing: --stamp FILE"
  exit 1
}

# Select appropriate command and flag to extract list of contents
case "$archive" in
  *.tar) list_contents=(tar tf) ;;
  *.tar.gz | *.tgz) list_contents=(tar tzf) ;;
  *.tar.bz2) list_contents=(tar tjf) ;;
  *) echo "Unhandled archive extension $archive" ; exit 1 ;;
esac

contents="$("${list_contents[@]}" "$archive" | tr '\n' ' ')"
cat <<EOF
ninja_dyndep_version = 1
build $stamp | $contents: dyndep
  restat = 1
EOF
