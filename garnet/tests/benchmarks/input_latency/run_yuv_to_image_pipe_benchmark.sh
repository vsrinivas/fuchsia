#!/boot/bin/sh
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Usage: run_yuv_to_image_pipe_benchmark.sh        \
#          --out_file <benchmark output file path> \
#          --benchmark_label <benchmark label>

set -o errexit -o nounset

while [ $# != 0 ]; do
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
kill_processes() {
  killall "root_presenter*" || true
  killall "scenic*" || true
  killall "basemgr*" || true
  killall "view_manager*" || true
  killall "flutter*" || true
  killall "present_view*" || true
  killall "yuv_to_image_pipe*" || true
}

kill_processes

echo "== $BENCHMARK_LABEL: Starting app..."
/bin/present_view fuchsia-pkg://fuchsia.com/yuv_to_image_pipe#meta/yuv_to_image_pipe.cmx \
  --NV12 --input_driven &

# Wait for yuv_to_image_pipe to start.
sleep 3

(
  sleep 1

  # Each tap will be 33.5ms apart, drifting 0.166ms against regular 60 fps
  # vsync interval. 100 taps span the entire vsync interval 1 time at 100
  # equidistant points.
  /bin/input tap 500 500 --tap_event_count=100 --duration=3350
) &

# Start tracing.
echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE
trace record --categories=input,gfx,magma --duration=5 --buffer-size=36 --output-file=$TRACE_FILE

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/garnet_input_latency_benchmarks/0/bin/process_input_latency_trace  \
  -test_suite_name="${BENCHMARK_LABEL}"                                            \
  -benchmarks_out_filename="${OUT_FILE}" "${TRACE_FILE}"

# Clean up by killing the processes.  Some of these were backgrounded; we
# want to prevent them from interfering with later performance tests.
kill_processes

echo "== $BENCHMARK_LABEL: Finished"
