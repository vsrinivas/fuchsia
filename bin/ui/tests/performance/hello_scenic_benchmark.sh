#!/boot/bin/sh
OUT_DIR=$1
OUT_FILE=$2
echo "== Killing processes..."
killall root_presenter; killall scenic; killall device_runner; killall view_manager; killall flutter*; killall set_root_view
echo "== Configuring scenic renderer params..."
set_renderer_params --clipping_enabled --render_continuously --screen_space_shadows
echo "== Tracing..."
DATE=`date +%Y-%m-%dT%H:%M:%S`
TRACE_FILE=$OUT_DIR/trace.$DATE.json
echo $TRACE_FILE
trace record --duration=10 --output-file=$TRACE_FILE hello_scenic
echo "== Processing trace..."
process_scenic_trace hello_scenic_benchmark $TRACE_FILE $OUT_FILE
