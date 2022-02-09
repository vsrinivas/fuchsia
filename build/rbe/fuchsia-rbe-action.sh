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

script_subdir="$(realpath --relative-to="$project_root" "$script_dir")"

# $PWD must be inside $project_root.
build_subdir="$(realpath --relative-to="$project_root" . )"
project_root_rel="$(realpath --relative-to=. "$project_root")"

# defaults
config="$script_dir"/fuchsia-re-client.cfg
# location of reclient binaries relative to output directory where build is run
reclient_bindir="$project_root_rel"/prebuilt/proprietary/third_party/reclient/linux-x64
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
  --dry-run: If set, print the computed rewrapper command without running it.

  --log: capture stdout/stderr to \$output_files[0].remote-log.
      This is useful when the stdout/stderr size exceeds RPC limits.

  --fsatrace-path: location of fsatrace tool (which must reside under
      'exec_root').  If provided, a remote trace will be created and
      downloaded as \$output_files[0].remote-fsatrace.

  --auto-reproxy: startup and shutdown reproxy around the command.
  --no-reproxy: assume reproxy is already running, and only use rewrapper.
      [This is the default.]

  All other options are forwarded to rewrapper until -- is encountered.
EOF
}

dry_run=0
log=0
fsatrace_path=
inputs=
output_files=
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
    --help|-h) usage; exit;;
    --dry-run) dry_run=1 ;;
    --log) log=1 ;;

    --cfg=*) config="$optarg" ;;
    --cfg) prev_opt=config ;;

    --bindir=*) reclient_bindir="$optarg" ;;
    --bindir) prev_opt=reclient_bindir ;;

    --fsatrace-path=*) fsatrace_path="$optarg" ;;
    --fsatrace-path) prev_opt=fsatrace_path ;;

    # Intercept --inputs and --output_files to allow for possible adjustments.
    --inputs=*) inputs="$optarg" ;;
    --inputs) prev_opt=inputs ;;
    --output_files=*) output_files="$optarg" ;;
    --output_files) prev_opt=output_files ;;

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

# env for remote execution
remote_env=/usr/bin/env

# Split up $inputs and $output_files for possible modification.
IFS="," read -r -a inputs_array <<< "$inputs"
IFS="," read -r -a output_files_array <<< "$output_files"

default_primary_output=rbe-action-output
if test "${#output_files_array[@]}" -gt 0
then
  primary_output="${output_files_array[0]}"
else
  # This name is not unique, but this is only used for tracing,
  # which should be investigated one action at a time.
  primary_output="$build_subdir/$default_primary_output"
fi
primary_output_rel="${primary_output#$build_subdir/}"

# When --log is requested, capture stdout/err to a log file remotely,
# and then download it.
log_wrapper=()
test "$log" = 0 || {
  logfile="$primary_output_rel.remote-log"
  log_wrapper+=( "$script_dir"/log-it.sh --log "$logfile" -- )
  inputs_array+=( "$script_subdir"/log-it.sh )
  output_files_array+=( "$build_subdir/$logfile" )
}

# Use fsatrace on the remote command, and also fetch the resulting log.
fsatrace_prefix=()
test -z "$fsatrace_path" || {
  # Adjust paths so that command is relative to $PWD, while rewrapper
  # parameters are relative to $project_root.

  fsatrace_relpath="$(realpath -s --relative-to="$project_root" "$fsatrace_path")"
  fsatrace_so="$fsatrace_relpath.so"
  inputs_array+=( "$fsatrace_relpath" "$fsatrace_so" )
  output_files_array+=( "$primary_output.remote-fsatrace" )
  fsatrace_prefix=(
    "$remote_env" FSAT_BUF_SIZE=5000000
    "$fsatrace_path" erwdtmq "$primary_output_rel.remote-fsatrace" --
  )
}

inputs_joined="$(IFS=, ; echo "${inputs_array[*]}")"
output_files_joined="$(IFS=, ; echo "${output_files_array[*]}")"

rewrapper_options+=(
  --inputs="$inputs_joined"
  --output_files="$output_files_joined"
)

# The remote command is in "$@".
rewrapped_command=(
  "$rewrapper"
  --cfg="$rewrapper_cfg"
  "${rewrapper_options[@]}"

  "${log_wrapper[@]}"
  "${fsatrace_prefix[@]}"
  "$@"
)

reproxy_prefix=()
# Prefix to startup and stop reproxy around this single command,
# which is needed if reproxy is not already running.
test "$want_auto_reproxy" = 0 ||
  reproxy_prefix=( "$auto_reproxy" --cfg="$reproxy_cfg" -- )

full_command=( "${reproxy_prefix[@]}" "${rewrapped_command[@]}" )

test "$dry_run" = 0 || {
  echo "[$script]:" "${full_command[@]}"
  exit
}
"${full_command[@]}"

# Exit normally on success.
status="$?"

case "$status" in
  35 | 45 | 137)
    # Retry once under these conditions:
    # 35: reclient error
    # 45: remote execution (server) error, e.g. remote blob download failure
    # 137: SIGKILL'd (signal 9) by OS.
    #   Reasons may include segmentation fault, or out of memory.
    # Successful retry will be silent.
    # Retries can be detected by looking for duplicate action digests
    # in reproxy logs, one will fail with REMOTE_ERROR.
    "${full_command[@]}"
    status="$?"
    ;;
  0) exit "$status" ;;
esac

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

