#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs tracing on the remote device.
#
# TODO(jeffbrown): Make this more robust overall, probably shouldn't
# bootstrap every time a program is supplied.  We could make this more
# self-contained too instead of relying on chrome://tracing.  At the least
# it would be nice to figure out how to provide a direct link to load the
# trace.

if [[ -z "${FUCHSIA_DIR}" ]] || [[ -z "${MAGENTA_TOOLS_DIR}" ]]; then
  echo "Fuchsia environment variables not defined" >&2
  echo "Please run 'source [fuchsia-root-dir]/scripts/env.sh' then 'fset'" >&2
  exit 1
fi

catapult_dir="${FUCHSIA_DIR}/third_party/catapult"
trace2html="${catapult_dir}/tracing/bin/trace2html"

function usage() {
  cat >&2 <<END
Usage: trace.sh [--duration N] [--bootstrap] [--env name] [--no-html]
                [program and args...]

Runs a trace and downloads the resulting trace file.

  --duration N : Traces for N seconds.  Defaults to 10 seconds.

  --bootstrap : Runs the progam within a freshly bootstrapped environment.

  --env name : Specifies the name of the environment within which to run
               the program.  Defaults to 'boot'.  Ignored if '--bootstrap'
               is also specified.

  --no-html : Does not automatically convert JSON trace file to HTML.
              The JSON file should be viewed using chrome://tracing manually.

  --buffer-size MB : Suggested trace buffer size for each provider in megabytes.
                     Defaults to 4 megabytes.

If a program is specified, it is started immediately after tracing begins.
END
}

duration=10
html=1
bootstrap=
env=boot
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
    --bootstrap)
      bootstrap=1
      ;;
    --env)
      if [[ $# -lt 2 ]]; then
        usage
        exit 1
      fi
      env=$2
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

trace="trace record --duration=${duration} --buffer-size=${buffer_size} $*"
command=
if [[ -n "${bootstrap}" ]]; then
  command="@ bootstrap ${trace}"
else
  command="@${env} ${trace}"
fi

# since we can't observe trace completion, add a delay to compensate for lag
delay=$(( $duration + 30 ))
trace_file="trace_$(date +%Y-%m-%d_at_%H.%M.%S).json"

rm -f "${trace_file}"
echo "Starting trace..." \
  && echo "Running: ${command}" \
  && "${MAGENTA_TOOLS_DIR}/netruncmd" : "${command}" \
  && echo "Sleeping ${delay} seconds..." \
  && sleep "${delay}" \
  && echo "Downloading trace to ${trace_file}" \
  && "${MAGENTA_TOOLS_DIR}/netcp" :/tmp/trace.json "${trace_file}"
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
