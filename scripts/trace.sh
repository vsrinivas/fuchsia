#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs tracing on the remote device.

if [[ -z "${FUCHSIA_DIR}" ]] || [[ -z "${FUCHSIA_BUILD_DIR}" ]]; then
  echo "Fuchsia environment variables not defined" >&2
  echo "Please run 'source [fuchsia-root-dir]/scripts/env.sh' then 'fset'" >&2
  exit 1
fi

catapult_dir="${FUCHSIA_DIR}/third_party/catapult"
trace2html="${catapult_dir}/tracing/bin/trace2html"

function usage() {
  cat >&2 <<END
Usage: trace.sh [--duration N] [--no-html] [program and args...]

Runs a trace and downloads the resulting trace file.

  --duration N : Traces for N seconds.  Defaults to 10 seconds.

  --no-html : Does not automatically convert JSON trace file to HTML.
              The JSON file should be viewed using chrome://tracing manually.

  --buffer-size MB : Suggested trace buffer size for each provider in megabytes.
                     Defaults to 4 megabytes.

If a program is specified, it is started immediately after tracing begins.
END
}

duration=10
html=1
buffer_size=4
while [[ $# -ne 0 ]]; do
  case $1 in
    --duration)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      duration=$2
      shift
      ;;
    --no-html)
      html=
      ;;
    --buffer-size)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      buffer_size=$2
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      break
  esac
  shift
done

command="trace record --duration=${duration} --buffer-size=${buffer_size} $*"

trace_file="trace_$(date +%Y-%m-%d_at_%H.%M.%S).json"
host="$(netaddr --fuchsia)"

rm -f "${trace_file}"
echo "Starting trace..." \
  && echo "Running: ${command}" \
  && ssh -q -F "${FUCHSIA_BUILD_DIR}/ssh-keys/ssh_config" "${host}" "${command}" \
  && echo "Downloading trace to ${trace_file}" \
  && scp -q -F "${FUCHSIA_BUILD_DIR}/ssh-keys/ssh_config" "[${host}]:/tmp/trace.json" "${trace_file}"
if [[ $? -ne 0 ]]; then
  echo "Failed" >&2;
  exit 1
fi

if [[ -n "${html}" ]]; then
  trace_html="${trace_file%.json}.html"
  trace_url="file://$(pwd)/${trace_html}"
  echo "Converting trace to HTML..." \
    && "${trace2html}" "${trace_file}" --output "${trace_html}" \
    && echo "Trace file: ${trace_url}"
else
  echo "Use chrome://tracing to view ${trace_file}"
fi
