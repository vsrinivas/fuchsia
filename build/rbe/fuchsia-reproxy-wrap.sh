#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# See usage().

set -e

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

# Configuration for RBE metrics and logs collection.
readonly fx_build_metrics_config="$project_root_rel"/.fx-build-metrics-config

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
  --cfg FILE: reclient config for reproxy
  --bindir DIR: location of reproxy tools
  All other flags before -- are forwarded to the reproxy bootstrap.
EOF
}

bootstrap_options=()
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
    # Forward all other options to reproxy
    *) bootstrap_options+=("$opt") ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

reproxy_cfg="$config"

bootstrap="$reclient_bindir"/bootstrap
reproxy="$reclient_bindir"/reproxy

# Establish a single log dir per reproxy instance so that statistics are
# accumulated per build invocation.
date="$(date +%Y%m%d-%H%M%S)"
reproxy_tmpdir="$(mktemp -d -t reproxy."$date".XXXX)"
# These environment variables take precedence over those found in --cfg.
export RBE_log_dir="$reproxy_tmpdir"
export RBE_proxy_log_dir="$reproxy_tmpdir"
# rbe_metrics.{pb,txt} appears in -output_dir
export RBE_output_dir="$reproxy_tmpdir"
# deps cache dir should be somewhere persistent between builds,
# and thus, not random.  /var/cache can be root-owned and not always writeable.
if test -n "$HOME"
then export RBE_deps_cache_dir="$HOME/.cache/reproxy/deps"
else export RBE_deps_cache_dir="/tmp/.cache/reproxy/deps"
fi
mkdir -p "$RBE_deps_cache_dir"

gcloud="$(which gcloud)" || {
  cat <<EOF
\`gcloud\` command not found (but is needed to authenticate).
\`gcloud\` can be installed from the Cloud SDK:

  http://go/cloud-sdk#installing-and-using-the-cloud-sdk

EOF
  exit 1
}

# Check authentication first.
# Instruct user to authenticate if needed.
"$gcloud" auth list 2>&1 | grep -q "$USER@google.com" || {
  cat <<EOF
Did not find credentialed account (\`gcloud auth list\`): $USER@google.com.
You may need to re-authenticate every 20 hours.

To authenticate, run:

  gcloud auth login --update-adc

EOF
  exit 1
}

# If configured, collect reproxy logs.
BUILD_METRICS_ENABLED=0
if [[ -f "$fx_build_metrics_config" ]]
then source "$fx_build_metrics_config"
fi

test "$BUILD_METRICS_ENABLED" = 0 || {
  if which uuidgen 2>&1 > /dev/null
  then build_uuid="$(uuidgen)"
  else
    cat <<EOF
'uuidgen' is required for logs collection, but missing.
On Debian/Ubuntu platforms, try: 'sudo apt install uuid-runtime'
EOF
    exit 1
  fi
}

# Startup reproxy.
# Use the same config for bootstrap as for reproxy.
# This also checks for authentication, and prompts the user to
# re-authenticate if needed.
"$bootstrap" --re_proxy="$reproxy" --cfg="$reproxy_cfg" "${bootstrap_options[@]}"

test "$BUILD_METRICS_ENABLED" = 0 || {
  # Pre-authenticate for uploading metrics and logs
  "$script_dir"/upload_reproxy_logs.sh --auth-only

  # Generate a uuid for uploading logs and metrics.
  echo "$build_uuid" > "$reproxy_tmpdir"/build_id
}

shutdown() {
  # b/188923283 -- added --cfg to shut down properly
  "$bootstrap" --shutdown --cfg="$reproxy_cfg"

  test "$BUILD_METRICS_ENABLED" = 0 || {
    # This script uses the 'bq' CLI tool, which is installed in the
    # same path as `gcloud`.
    # This is experimental and runs a bit noisily for the moment.
    # TODO(https://fxbug.dev/93886): make this run silently
    cloud_project=fuchsia-engprod-metrics-prod
    dataset=metrics
    "$script_dir"/upload_reproxy_logs.sh \
      --reclient-bindir="$reclient_bindir" \
      --uuid="$build_uuid" \
      --bq-logs-table="$cloud_project:$dataset".rbe_client_command_logs_developer \
      --bq-metrics-table="$cloud_project:$dataset".rbe_client_metrics_developer \
      "$reproxy_tmpdir"
      # The upload exit status does not propagate from inside a trap call.
  }
}

# EXIT also covers INT
trap shutdown EXIT

# original command is in "$@"
# Do not 'exec' this, so that trap takes effect.
"$@"
