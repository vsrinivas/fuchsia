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
reclient_bindir="$script_dir"/../../prebuilt/proprietary/third_party/reclient/linux-x64

usage() {
  cat <<EOF
$script [reproxy options] -- command [args...]

This script runs reproxy around another command(s).
reproxy is a proxy process between reclient and a remote back-end (RBE).
It needs to be running to host rewrapper commands, which are invoked
by a build system like 'make' or 'ninja'.

example:
  $script -- ninja

options:
  --cfg FILE: reclient config for reproxy and rewrapper tools
  --bindir DIR: location of reproxy and rewrapper tools
EOF
}

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
    # stop option processing
    --) shift; break ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

reproxy_cfg="$config"
rewrapper_cfg="$config"

bootstrap="$reclient_bindir"/bootstrap
reproxy="$reclient_bindir"/reproxy

# Use the same config for bootstrap as for reproxy
"$bootstrap" --re_proxy="$reproxy" --cfg="$reproxy_cfg"
# b/188923283 -- added --cfg to shut down properly
shutdown() {
  "$bootstrap" --shutdown --cfg="$reproxy_cfg"
}

# EXIT also covers INT
trap shutdown EXIT

# original command is in "$@"
# Do not 'exec' this, so that trap takes effect.
"$@"
