#!/boot/bin/sh
#
# Usage: scenic_benchmark.sh
#          --out_dir <trace output dir>
#          --out_file <benchmark output file path>
#          --benchmark_label <benchmark label>
#          --cmd <cmd to benchmark>
#          (optional) --flutter_app_name <flutter application name>
#          (optional) --sleep_before_trace <duration>
#          [renderer_params...]
#
# See renderer_params.cc for more arguments.
#

# By default, there is no flutter app name (process_scenic_trace.go interprets
# the empty string as no flutter app).
FLUTTER_APP_NAME=''

# How long to sleep before starting the trace.  See usage below for further
# details on why this is sometimes required.
SLEEP_BEFORE_TRACE=''

while [ "$1" != "" ]; do
  case "$1" in
    --out_dir)
      OUT_DIR="$2"
      shift
      ;;
    --out_file)
      OUT_FILE="$2"
      shift
      ;;
    --benchmark_label)
      BENCHMARK_LABEL="$2"
      shift
      ;;
    --cmd)
      CMD="$2"
      shift
      ;;
    --flutter_app_name)
      FLUTTER_APP_NAME="$2"
      shift
      ;;
    --sleep_before_trace)
      SLEEP_BEFORE_TRACE="$2"
      shift
      ;;
    *)
      break
      ;;
  esac
  shift
done

RENDERER_PARAMS=$@

DATE=`date +%Y-%m-%dT%H:%M:%S`
TRACE_FILE=$OUT_DIR/trace.$DATE.json

echo "== $BENCHMARK_LABEL: Killing processes..."
killall root_presenter*; killall scenic*; killall basemgr*; killall view_manager*; killall flutter*; killall set_root_view*

echo "== $BENCHMARK_LABEL: Configuring scenic renderer params..."
/pkgfs/packages/run/0/bin/run fuchsia-pkg://fuchsia.com/set_renderer_params#meta/set_renderer_params.cmx --render_continuously $RENDERER_PARAMS

echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE
# Use `run` to ensure the command has the proper environment.
/pkgfs/packages/run/0/bin/run $CMD &

# For certain benchmarks (such as ones that depend on Flutter events), we need
# to sleep a bit here to let the Flutter processes start before we begin
# tracing, so that thread/process names will be present.  TODO(ZX-2706): This
# sleep can be removed once ZX-2706 is resolved.
if [ "${SLEEP_BEFORE_TRACE}" != '' ]; then
  sleep "${SLEEP_BEFORE_TRACE}"
fi

# Only trace required events, which reduces trace size and prevents trace
# buffer overflow. TODO(ZX-1107): Overflow might be fixed with continuous
# tracing.
trace record --categories='gfx,flutter,kernel:meta' --duration=10 --buffer-size=12 --output-file=$TRACE_FILE

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace \
  -flutter_app_name="${FLUTTER_APP_NAME}" \
  -test_suite_name="${BENCHMARK_LABEL}" \
  -benchmarks_out_filename="${OUT_FILE}" \
  "${TRACE_FILE}"

echo "== $BENCHMARK_LABEL: Finished processing trace."
