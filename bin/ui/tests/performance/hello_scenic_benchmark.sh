#!/boot/bin/sh
#
# Usage: hello_scenic_benchmark.sh <trace output dir> <benchmark output file path>
#
OUT_DIR=$1
OUT_FILE=$2
TRACE_FILE=$OUT_DIR/trace.$DATE.json

echo "== hello_scenic_benchmark: Killing processes..."
killall root_presenter; killall scenic; killall device_runner; killall view_manager; killall flutter*; killall set_root_view

echo "== hello_scenic_benchmark: Configuring scenic renderer params..."
set_renderer_params --clipping_enabled --render_continuously --screen_space_shadows

echo "== hello_scenic_benchmark: Tracing..."
DATE=`date +%Y-%m-%dT%H:%M:%S`
echo $TRACE_FILE
trace record --duration=10 --output-file=$TRACE_FILE hello_scenic

echo "== hello_scenic_benchmark: Processing trace..."
/pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace hello_scenic_benchmark $TRACE_FILE $OUT_FILE
