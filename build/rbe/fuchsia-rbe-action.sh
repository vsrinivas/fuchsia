#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# See usage().

set -uo pipefail

script="$0"
script_dir="$(dirname "$script")"

# The project_root must cover all inputs, prebuilt tools, and build outputs.
# This should point to $FUCHSIA_DIR for the Fuchsia project.
# ../../ because this script lives in build/rbe.
# The value is an absolute path.
project_root="$(readlink -f "$script_dir"/../..)"

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
prev_opt=
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
test -z "$prev_opt" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

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

# Exit normally on success.
status="$?"
test "$status" != 0 || exit "$status"

# Diagnostics: Suggest where to look, and possible actions.
# Look for symptoms inside reproxy's log, where most of the action is.
tmpdir="${RBE_proxy_log_dir:-/tmp}"
reproxy_errors="$tmpdir"/reproxy.ERROR
echo "The last lines of $reproxy_errors might explain a remote failure:"
if test -r "$reproxy_errors" ; then tail "$reproxy_errors" ; fi

if grep -q "Fail to dial" "$reproxy_errors"
then
  cat <<EOF
"Fail to dial" could indicate that reproxy is not running.
Did you run with 'fx build'?
If not, you may need to wrap your build command with:

$project_root/build/rbe/fuchsia-reproxy-wrap.sh -- YOUR-COMMAND

'Proxy started successfully.' indicates that reproxy is running.

EOF
fi

if grep -q "Error connecting to remote execution client: rpc error: code = PermissionDenied" "$reproxy_errors"
then
  cat <<EOF
You might not have permssion to access the RBE instance.
Contact fuchsia-build-team@google.com for support.

EOF
fi

mapfile -t local_missing_files < <(grep "Status:LocalErrorResultStatus.*: no such file or directory" "$reproxy_errors" | sed -e 's|^.*Err:stat ||' -e 's|: no such file.*$||')
test "${#local_missing_files[@]}" = 0 || {
cat <<EOF
The following files are expected to exist locally for uploading,
but were not found:

EOF
for f in "${local_missing_files[@]}"
do
  f_rel="$(echo "$f" | sed "s|^$project_root/||")"
  case "$f_rel" in
    out/*) echo "  $f_rel (generated file: missing dependency?)" ;;
    *) echo "  $f_rel (source)" ;;
  esac
done
}

exit "$status"

