#!/bin/bash
#
# trace_from_host.sh
# See below for usage.
#
OUT=$1
if [ -z "$OUT" ]
then
  cat << USAGE
Usage:
trace_from_host.sh base_file_name [renderer_params...]

--nokill    Will not kill scenic/device_runner

See renderer_params.cc for more arguments.

example: trace_from_host.sh trace_clipping_shadows --clipping_enabled --screen_space_shadows

USAGE
exit;
fi
shift # swallow first argument

ALL_ARGS="${@}"
if ! [[ ${ALL_ARGS} =~ "--nokill" ]]
then
  echo "Killing processes and setting renderer params: $@"
  (set -x; fx shell "killall root_presenter; killall scenic; killall device_runner; killall view_manager; killall flutter*; killall set_root_view")
  (set -x; fx shell "set_renderer_params --render_continuously $@")
  echo "Press control-C after device_runner starts..."
  (set -x; fx shell "run device_runner")
  echo "Press 'return' to start tracing..."
  read
fi
DATE=`date +%Y-%m-%dT%H:%M:%S`
echo "Tracing..."
(set -x; fx shell trace record --duration=10 --output-file=/tmp/trace-$OUT.json)
(set -x; fx scp [$(fx netaddr --fuchsia)]:/tmp/trace-$OUT.json trace-$OUT.json)
(set -x; go run $FUCHSIA_ROOT/garnet/bin/ui/tests/performance/process_scenic_trace.go $OUT trace-$OUT.json benchmarks-$OUT.json)

