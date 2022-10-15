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

# realpath doesn't ship with Mac OS X (provided by coreutils package).
# We only want it for calculating relative paths.
# Work around this using Python.
if which realpath 2>&1 > /dev/null
then
  function relpath() {
    local -r from="$1"
    local -r to="$2"
    # Preserve symlinks.
    realpath -s --relative-to="$from" "$to"
  }
else
  # Point to our prebuilt python3.
  python="$(ls "$project_root"/prebuilt/third_party/python3/*/bin/python3)" || {
    echo "*** Python interpreter not found under $project_root/prebuilt/third_party/python3."
    exit 1
  }
  function relpath() {
    local -r from="$1"
    local -r to="$2"
    "$python" -c "import os; print(os.path.relpath('$to', start='$from'))"
  }
fi

script_subdir="$(relpath "$project_root" "$script_dir")"

check_output_dir_leaks="$script_dir"/output-scanner.sh

# $PWD must be inside $project_root.
build_subdir="$(relpath "$project_root" . )"
project_root_rel="$(relpath . "$project_root")"

# defaults
config="$script_dir"/fuchsia-re-client.cfg

detected_os="$(uname -s)"
case "$detected_os" in
  Darwin) readonly PREBUILT_OS="mac" ;;
  Linux) readonly PREBUILT_OS="linux" ;;
  *) echo >&2 "Unknown operating system: $detected_os" ; exit 1 ;;
esac

detected_arch="$(uname -m)"
case "$detected_arch" in
  x86_64) readonly PREBUILT_ARCH="x64" ;;
  *) echo >&2 "Unknown machine architecture: $detected_arch" ; exit 1 ;;
esac

PREBUILT_SUBDIR="$PREBUILT_OS"-"$PREBUILT_ARCH"

# location of reclient binaries relative to output directory where build is run
reclient_bindir="$project_root_rel"/prebuilt/proprietary/third_party/reclient/"$PREBUILT_SUBDIR"

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

  --label STRING: build system identifier, for diagnostic messages.

  --log: capture stdout/stderr to \$output_files[0].remote-log.
      This is useful when the stdout/stderr size exceeds RPC limits.
  --log=LOGBASE: same as --log, but uses LOGBASE as the log basename.

  --save-temps: keep around cleaned up intermediate files

  --fsatrace-path: location of fsatrace tool (which must reside under
      'exec_root').  If provided, a remote trace will be created and
      downloaded as \$output_files[0].remote-fsatrace.

  --auto-reproxy: startup and shutdown reproxy around the command.
  --no-reproxy: assume reproxy is already running, and only use rewrapper.
      [This is the default.]

  --diagnose-nonzero, --no-diagnose-nonzero:
      On nonzero exit statuses, attempt to diagnose potential RBE issues.
      This scans reproxy logs for information, and can be noisy.
      Default: --diagnose-nonzero


  All other options are forwarded to rewrapper until -- is encountered.

Detected parameters:
  exec_root=$project_root
EOF
}

dry_run=0
log=0
fsatrace_path=
inputs=
output_files=
rewrapper_options=()
want_auto_reproxy=0
canonicalize_working_dir="false"
exec_strategy=
diagnose_nonzero_exit=0
label=
logbase=
save_temps=0

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
    --log=*) log=1 ; logbase="$optarg";;
    --log) log=1 ;;

    --cfg=*) config="$optarg" ;;
    --cfg) prev_opt=config ;;

    --bindir=*) reclient_bindir="$optarg" ;;
    --bindir) prev_opt=reclient_bindir ;;

    --label=*) label="$optarg" ;;
    --label) prev_opt=label ;;

    --save-temps) save_temps=1 ;;

    --fsatrace-path=*) fsatrace_path="$optarg" ;;
    --fsatrace-path) prev_opt=fsatrace_path ;;

    # Intercept --inputs and --output_files to allow for possible adjustments.
    --inputs=*) inputs="$optarg" ;;
    --inputs) prev_opt=inputs ;;
    --output_files=*) output_files="$optarg" ;;
    --output_files) prev_opt=output_files ;;

    # Detect this option, and forward it to rewrapper:
    --canonicalize_working_dir=*)
      canonicalize_working_dir="$optarg"
      rewrapper_options+=( "$opt" )
      ;;

    # Detect this option, and forward it to rewrapper:
    --exec_strategy=*)
      exec_strategy="$optarg"
      rewrapper_options+=( "$opt" )
      ;;

    --diagnose-nonzero) diagnose_nonzero_exit=1 ;;
    --no-diagnose-nonzero) diagnose_nonzero_exit=0 ;;

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
IFS="," inputs_array=($inputs)
IFS="," output_files_array=($output_files)

if test "${#output_files_array[@]}" -gt 0
then
  primary_output_name="${output_files_array[0]}"
else
  # The default name is not necessarily unique, but this is fine when
  # tracing one action at a time.
  primary_output_name="$build_subdir/${logbase:-"rbe-action-output"}"
fi
primary_output_rel="${primary_output_name#$build_subdir/}"

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

  fsatrace_relpath="$(relpath "$project_root" "$fsatrace_path")"
  fsatrace_so="$fsatrace_relpath.so"
  inputs_array+=( "$fsatrace_relpath" "$fsatrace_so" )
  output_files_array+=( "$primary_output_name.remote-fsatrace" )
  fsatrace_prefix=(
    "$remote_env" FSAT_BUF_SIZE=5000000
    "$fsatrace_path" erwdtmq "$primary_output_rel.remote-fsatrace" --
  )
}

# Workaround bash treating an empty array as unset in some versions: ${arr[@]+"${arr[@]}"}
# https://stackoverflow.com/questions/7577052/bash-empty-array-expansion-with-set-u
inputs_joined="$(IFS=, echo "${inputs_array[*]+"${inputs_array[*]}"}")"
output_files_joined="$(IFS=, echo "${output_files_array[*]+"${output_files_array[*]}"}")"

output_files_rel=()
for f in "${output_files_array[@]+"${output_files_array[@]}"}"
do output_files_rel+=( "${f#$build_subdir/}" )
done

rewrapper_options+=(
  --inputs="$inputs_joined"
  --output_files="$output_files_joined"
)

# The remote command is in "$@".
rewrapped_command=(
  "$rewrapper"
  --cfg="$rewrapper_cfg"
  --exec_root="$project_root"
  "${rewrapper_options[@]+"${rewrapper_options[@]}"}"

  "${log_wrapper[@]+"${log_wrapper[@]}"}"
  "${fsatrace_prefix[@]+"${fsatrace_prefix[@]}"}"
  "$@"
)

reproxy_prefix=()
# Prefix to startup and stop reproxy around this single command,
# which is needed if reproxy is not already running.
test "$want_auto_reproxy" = 0 ||
  reproxy_prefix=(
    "$auto_reproxy"
    --bindir="$reclient_bindir"
    --cfg="$reproxy_cfg"
    --
  )

full_command=( "${reproxy_prefix[@]+"${reproxy_prefix[@]}"}" "${rewrapped_command[@]}" )

if test -n "$label"
then message_header="[$script][$label]:"
else message_header="[$script]:"
fi

function message() {
  echo "$message_header" "$@"
}

test "$dry_run" = 0 || {
  message "${full_command[@]}"
  exit
}

# Pre-flight check for output dir leaks in the remote command.
# Enforce this when using --canonicalize_working_dir.
# We could not just wrap the rewrapper command with the same script
# because rewrapper's --output_files arguments do need to be relative to the
# *exec_root*, and thus, contain the $build_subdir in their paths' prefixes.
test "$canonicalize_working_dir" = "false" || \
  "$check_output_dir_leaks" --no-execute "${output_files_rel[@]}" -- "$@" || {
  message "Error: Detected local output dir leaks in the command.  Aborting remote execution."
  exit 1
}

# Give stderr file a unique name based on primary output to avoid collisions.
stderr_file="$primary_output_rel".stderr

# Copy stderr to a temporary file for diagnostic analysis.
"${full_command[@]}" 2> >(tee "$stderr_file" >&2)
# Preserve exit status from the rewrapper command.
status="$?"

trap cleanup EXIT

function cleanup() {
  test "$save_temps" = 1 || rm -f "$stderr_file"
}

if grep -q "fatal error:.*file not found" "$stderr_file"
then :
  # Do not retry.  A local file is missing.
else
  case "$status" in
    35 | 45 | 137)
      # Retry once under these conditions:
      # 35: reclient error (includes infrastructural issues or local errors)
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
fi

# From a nonzero exit code alone, it is difficult to distinguish a command
# error from an infrastructure error.  We can attempt to scrape information
# from the reproxy log, but this is fragile.
test "$diagnose_nonzero_exit" = 0 || exit "$status"

# Do not attempt to diagnose remote issues when using local execution.
case "$exec_strategy" in
  local) exit "$status" ;;
  remote_local_fallback) exit "$status" ;;
esac

# Remote execution diagnostics: Suggest where to look, and possible actions.
# Look for symptoms inside reproxy's log, where most of the action is.
# TODO(b/241306350): inspect an individual action's log, not global log.
tmpdir="${RBE_proxy_log_dir:-/tmp}"
reproxy_errors="$tmpdir"/reproxy.ERROR
message "The last lines of $reproxy_errors might explain a remote failure:"
if test -r "$reproxy_errors" ; then tail "$reproxy_errors" ; fi

if grep -q "Fail to dial" "$reproxy_errors"
then
  cat <<EOF
$message_header:
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
$message_header:
You might not have permssion to access the RBE instance.
Contact fuchsia-build-team@google.com for support.

EOF
fi

# TODO(b/201697587): surface this diagnostic from rewrapper
local_missing_files=( $(grep "Status:LocalErrorResultStatus.*: no such file or directory" "$reproxy_errors" | sed -e 's|^.*Err:stat ||' -e 's|: no such file.*$||') )
test "${#local_missing_files[@]}" = 0 || {
cat <<EOF
$message_header:
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

