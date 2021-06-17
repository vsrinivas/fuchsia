#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# See usage().

set -e

script="$0"
script_dir="$(dirname "$script")"

# defaults
config="$script_dir"/fuchsia-re-client.cfg
# location of reclient binaries relative to output directory where build is run
# TODO(fangism): this could also be set relative to --exec_root.
reclient_bindir="$script_dir"/../../prebuilt/proprietary/third_party/reclient/linux-x64
auto_reproxy="$script_dir"/fuchsia-reproxy-wrap.sh

usage() {
  cat <<EOF
$script [rewrapper options] -- command [args...]

This script runs a command remotely (RBE) using rewrapper.

example: $script -- echo Hello

options:
  --cfg FILE: reclient config for both reproxy and rewrapper tools
      [default: $config]
  --bindir DIR: location of reproxy and rewrapper tools
      [default: $reclient_bindir]

  --auto-reproxy: startup and shutdown reproxy around the command.
  --no-reproxy: assume reproxy is already running, and only use rewrapper.
      [This is the default.]

  All other options are forwarded to rewrapper until -- is encountered.
EOF
}

rewrapper_options=()
want_auto_reproxy=0
# Extract script options before --
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
    --cfg=*) config="$optarg" ;;
    --cfg) prev_opt=config ;;
    --bindir=*) reclient_bindir="$optarg" ;;
    --bindir) prev_opt=reclient_bindir ;;
    --auto-reproxy) want_auto_reproxy=1 ;;
    --no-reproxy) want_auto_reproxy=0 ;;
    # stop option processing
    --) shift; break ;;
    # Forward all other options to rewrapper
    *) rewrapper_options+=("$opt") ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

reproxy_cfg="$config"
rewrapper_cfg="$config"

rewrapper="$reclient_bindir"/rewrapper

# command is in "$@"
rewrapped_command=("$rewrapper" --cfg="$rewrapper_cfg" "${rewrapper_options[@]}" "$@")

if test "$want_auto_reproxy" = 1
then
  # startup and stop reproxy around this single command
  "$auto_reproxy" --cfg="$reproxy_cfg" -- "${rewrapped_command[@]}"
else
  # reproxy is already running
  "${rewrapped_command[@]}"
fi

