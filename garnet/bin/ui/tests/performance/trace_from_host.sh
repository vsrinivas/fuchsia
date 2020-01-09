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

1. Kills basemgr and all scenic-related processes.
2. Prompts the user (giving time to put the system in a specific state)
3. Takes a trace
4. Outputs:
     trace: trace-$base_file_name.out
     scenic performance stats: benchmarks-$base_file_name.out

Options:

--nokill    Will not kill scenic/basemgr

Flags for renderer_params:

--unshadowed
--stencil_shadow_volume

--clipping_enabled
--clipping_disabled

See renderer_params.cc for more arguments.



example: trace_from_host.sh trace_clipping_shadows --clipping_enabled --stencil_shadow_volume

USAGE
exit;
fi
shift # swallow first argument

ALL_ARGS="${@}"
if ! [[ ${ALL_ARGS} =~ "--nokill" ]]
then
  echo "Killing processes and setting renderer params: $@"
  (set -x; fx shell "killall root_presenter; killall scenic; killall basemgr; killall flutter*")
  (set -x; fx shell "fuchsia-pkg://fuchsia.com/set_renderer_params#meta/set_renderer_params.cmx --render_continuously $@")
  echo "== Press control-C to start tracing (after basemgr starts.) =="
  (set -x; fx shell "run fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx &")
else
  (set -x; fx shell "fuchsia-pkg://fuchsia.com/set_renderer_params#meta/set_renderer_params.cmx --render_continuously $@")
  sleep 2
fi
DATE=`date +%Y-%m-%dT%H:%M:%S`
echo "Tracing..."
(set -x; fx shell trace record --categories=gfx --buffer-size=8 --duration=10 --output-file=/tmp/trace-$OUT.json)
(set -x; fx scp [$(fx get-device-addr)]:/tmp/trace-$OUT.json trace-$OUT.json)
(set -x; fx shell rm /tmp/trace-$OUT.json)
(set -x; go run $FUCHSIA_DIR/garnet/bin/ui/tests/performance/process_gfx_trace.go -test_suite_name="${OUT}" -benchmarks_out_filename="benchmarks-${OUT}.json" "trace-${OUT}.json")
