#!/boot/bin/sh
#
# Usage: scenic_benchmark.sh <trace output dir> <benchmark output file path>
#            <benchmark label> <cmd to benchmark> [renderer_params...]
#
# See renderer_params.cc for more arguments.
#
OUT_DIR=$1
OUT_FILE=$2
BENCHMARK_LABEL=$3
DATE=`date +%Y-%m-%dT%H:%M:%S`
CMD=$4
shift # swallow first argument
shift # swallow second argument
shift # swallow third argument
shift # swallow fourth argument
RENDERER_PARAMS=$@

TRACE_FILE=$OUT_DIR/trace.$DATE.json

echo "== $BENCHMARK_LABEL: Killing processes..."
killall root_presenter; killall scenic; killall device_runner; killall view_manager; killall flutter*; killall set_root_view

echo "== $BENCHMARK_LABEL: Configuring scenic renderer params..."
set_renderer_params --render_continuously $RENDERER_PARAMS

echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE
$CMD &
trace record --duration=10 --buffer-size=12 --output-file=$TRACE_FILE

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace $BENCHMARK_LABEL $TRACE_FILE $OUT_FILE
