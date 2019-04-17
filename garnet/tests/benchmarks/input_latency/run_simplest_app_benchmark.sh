#!/boot/bin/sh
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Usage: run_simplest_app_benchmark.sh             \
#          --out_file <benchmark output file path> \
#          --benchmark_label <benchmark label>

while [ "$1" != "" ]; do
  case "$1" in
    --out_file)
      OUT_FILE="$2"
      shift
      ;;
    --benchmark_label)
      BENCHMARK_LABEL="$2"
      shift
      ;;
    *)
      break
      ;;
  esac
  shift
done

TRACE_FILE="/tmp/trace-$(date +%Y-%m-%dT%H:%M:%S).json"

echo "== $BENCHMARK_LABEL: Killing processes..."
killall root_presenter* || true
killall scenic* || true
killall basemgr* || true
killall flutter* || true
killall present_view* || true

echo "== $BENCHMARK_LABEL: Starting app..."
/bin/run -d fuchsia-pkg://fuchsia.com/simplest_app#meta/simplest_app.cmx

sleep 3

# Start tracing.
echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE
trace record --categories=input,gfx,magma --duration=5 --buffer-size=12 --output-file=$TRACE_FILE &

sleep 1

# Each tap will be 33.5ms apart, drifting 0.166ms against regular 60 fps vsync
# interval. 100 taps span the entire vsync interval 1 time at 100 equidistant
# points.
/bin/input --tap_event_count=100 --duration=3350 tap 500 500

sleep 15

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/garnet_input_latency_benchmarks/0/bin/process_input_latency_trace  \
  -test_suite_name="${BENCHMARK_LABEL}"                                            \
  -benchmarks_out_filename="${OUT_FILE}" "${TRACE_FILE}"
