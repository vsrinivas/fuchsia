#!/boot/bin/sh
#
# Usage: scenic_benchmark.sh
#          --out_dir <trace output dir>
#          --out_file <benchmark output file path>
#          --benchmark_label <benchmark label>
#          --cmd <cmd to benchmark>
#          (optional) --flutter_app_name <flutter application name>
#          [renderer_params...]
#
# See renderer_params.cc for more arguments.
#

# By default, there is no flutter app name (process_scenic_trace.go interprets
# the empty string as no flutter app).
FLUTTER_APP_NAME=''

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
/pkgfs/packages/run/0/bin/run set_renderer_params --render_continuously $RENDERER_PARAMS

echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE
# Use `run` to ensure the command has the proper environment.
/pkgfs/packages/run/0/bin/run $CMD &

# Only trace 'gfx' events which reduces trace size and prevents trace buffer overflow.
# TODO(ZX-1107): Overflow might be fixed with continuous tracing.
trace record --categories=gfx --duration=10 --buffer-size=12 --output-file=$TRACE_FILE

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace \
  -flutter_app_name="${FLUTTER_APP_NAME}" \
  -test_suite_name="${BENCHMARK_LABEL}" \
  -benchmarks_out_filename="${OUT_FILE}" \
  "${TRACE_FILE}"

echo "== $BENCHMARK_LABEL: Finished processing trace."
