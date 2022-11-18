#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly CYAN='\033[1;36m'
readonly DARK='\033[0;90m'
readonly WHITE='\033[1;37m'
readonly NC='\033[0m' # No Color

categories="memory_profile"
pprof_path="pprof"
output_dir="${HOME}/memory_traces"
fuchsia_dir="${FUCHSIA_DIR:-${HOME}/fuchsia}"
readonly default_build_id_dir="${fuchsia_dir}/out/default/.build-id"
build_id_input_dir="$default_build_id_dir"
circular=false
buffer_size=70

function usage() {
  echo "$(basename $0) [--categories <categories>] [--circular <buffer-size>] [--output-dir <directory>] [--build-id <directory>] [--pprof <path>]"
  echo
  echo "Records a trace, converts it to pprof format, created a build id direcotry, and starts pprof."
  echo
  echo "Options:"
  echo "  --categories comma-separated list of categories to enable."
  echo "  --circular   returns only the latest event at the time the trace stops."
  echo "  --build-id   directory containing debug symbols, using `/xx/xxxxxxxx.debug` layout."
  echo "               Default: ${default_build_id_dir}"
  echo "  --output-dir directory where to write profiles."
  echo "               Default: ${output_dir}"
  echo "  --pprof      path to the ppfor tool."
  echo "               Default: ${pprof_path}"
}

while [[ $# -gt 0 ]]; do
  case $1 in
    --categories)
      categories="$2"
      shift # past argument
      shift # past value
      ;;
    --build-id)
      if [[ ! -d "$2" ]]; then
        echo "ERROR: Directory not found: $2"
        exit 1
      fi
      build_id_input_dir="$2"
      shift # past argument
      shift # past value
      ;;
    --circular)
      circular=true
      buffer_size="$2"
      shift # past argument
      shift # past value
      ;;
    --output-dir)
      if [[ ! -d "$2" ]]; then
        echo "ERROR: Build id directory not found: $2"
        exit 1
      fi
      output_dir="$2"
      shift # past argument
      shift # past value
      ;;
    --pprof)
      if [[ ! -f "$2" ]]; then
        echo "ERROR: File not found: $2"
        exit 1
      fi
      pprof_path="$2"
      shift # past argument
      shift # past value
      ;;
    -*|--*)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

readonly fxt_to_pprof="${fuchsia_dir}/out/default/host_x64/fxt_to_pprof"
if [[ ! -f "${fxt_to_pprof}" ]]; then
  echo "ERROR: fxt_to_pprof tool not found at ${fxt_to_pprof}"
  echo "Consider to add the following option to fx set: --with-host //src/performance/memory/profile:fxt_to_pprof"
  exit 1
fi

if [[ ! -d "${build_id_input_dir}" ]]; then
  echo "ERROR: Build id directory not found: $2"
  exit 1
fi

function show_and_run() {
  echo
  echo -e "${CYAN}[Run]${WHITE} $@${NC}"
  echo
  bash -c "$*"
}

trace_path="${output_dir}/$(date +%Y-%m-%d-%H%M%S)"
mkdir -p "${output_dir}"

if $circular
then
  show_and_run ffx trace start --buffering-mode streaming --categories "${categories}" --output "${trace_path}.head" --background
  head_size=0
  while [[ $(stat --format=%s "${trace_path}.head") -lt 1024 ]]
  do
    echo "Polling ${trace_path}.head intil it gets some content."
    sleep 1
  done
  show_and_run ffx trace stop
  show_and_run ffx trace start --buffering-mode circular --buffer-size "${buffer_size}" --categories "${categories}" --output "${trace_path}"

  show_and_run "${fxt_to_pprof}" "${trace_path}" "${trace_path}.head"
else
  # Ensure the background job is stopped when the process exits abruptly.
  trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT
  (
    sleep 5
    echo
    echo -e 'Size of the trace file (\u2197 as samples are collected):'
    while true
    do
      echo -e "${DARK}  => $(ls -hs "${trace_path}")${NC}"
      sleep 4
    done
  ) &
  backgroup_pid=$!

  show_and_run ffx trace start --buffering-mode streaming --categories "${categories}" --output "${trace_path}"
  kill "${backgroup_pid}"

  show_and_run "${fxt_to_pprof}" "${trace_path}"
fi

show_and_run gzip "${trace_path}.pb"
show_and_run PPROF_BINARY_PATH="${build_id_input_dir}" "${pprof_path}" -flame "${trace_path}.pb.gz"

