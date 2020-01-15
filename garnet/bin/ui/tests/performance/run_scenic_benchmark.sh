#!/boot/bin/sh
#
# Usage: scenic_benchmark.sh
#          --out_file <benchmark output file path>
#          --benchmark_label <benchmark label>
#          --cmd <cmd to benchmark, (commands separated by ; will be run separately)>
#          (optional) --flutter_app_names <flutter application name>
#          (optional) --all_apps
#          (optional) --sleep_before_trace <duration>
#          (optional) --extra_categories <category names, separated by ,>
#          (optional) --trace_duration <duration>
#          (optional) --buffer_size <trace buffer size>
#          [renderer_params...]
#
# See renderer_params.cc for more arguments.
#

# By default, there is no flutter app name (process_gfx_trace.go interprets
# the empty string as no flutter app).
FLUTTER_APP_NAMES=''

# Inspect all running apps. Default to false if all_apps flag isn't set. Overrides FLUTTER_APP_NAMES.
ALL_APPS="false"

# How long to sleep before starting the trace.  See usage below for further
# details on why this is sometimes required.
SLEEP_BEFORE_TRACE=''

# What categories to track. The defaults are the ones needed
# for process_gfx_trace to function.
CATEGORIES='gfx,flutter,kernel:meta'

# Duration of the trace
DURATION=10

# Buffer size
BUFFER_SIZE=12

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
    --cmd)
      CMD="$2"
      shift
      ;;
    --flutter_app_names)
      FLUTTER_APP_NAMES="$2"
      shift
      ;;
    --all_apps)
      ALL_APPS="true"
      ;;
    --sleep_before_trace)
      SLEEP_BEFORE_TRACE="$2"
      shift
      ;;
    --extra_categories)
      CATEGORIES="$CATEGORIES,$2"
      shift
      ;;
    --trace_duration)
      DURATION="$2"
      shift
      ;;
    --buffer_size)
      BUFFER_SIZE="$2"
      shift
      ;;
    *)
      break
      ;;
  esac
  shift
done

# Split commands on delimiter and eval them one by one
eval_commands () {
  delimiter=';'
  command="$1"
  while [ ! -z "$command" ]; do
    subcmd=${command%%$delimiter*}
    command=${command##$subcmd}
    # Remove any leading delimiters
    first_letter=$(echo $command | cut -c1)
    while [ "$first_letter" = "$delimiter" ]; do
      command=$(echo $command | sed 's/^.//')
      first_letter=$(echo $command | cut -c1)
    done
    eval $subcmd
  done
}

RENDERER_PARAMS=$@

DATE=`date +%Y-%m-%dT%H:%M:%S`
TRACE_FILE="/tmp/trace-${DATE}.json"

kill_processes() {
  echo "== $BENCHMARK_LABEL: Killing processes..."
  killall root_presenter*
  killall scenic*
  killall basemgr*
  killall flutter*
  killall present_view*
}

kill_processes

echo "== $BENCHMARK_LABEL: Configuring scenic renderer params..."
/pkgfs/packages/run/0/bin/run fuchsia-pkg://fuchsia.com/set_renderer_params#meta/set_renderer_params.cmx --render_continuously $RENDERER_PARAMS

echo "== $BENCHMARK_LABEL: Tracing..."
echo $TRACE_FILE

# Check if cmd contains any semicolons and use eval if it does, otherwise use run
echo "Running command:"
echo "$CMD"

if echo "$CMD" | grep -q ";"; then
  eval_commands "$CMD"
else
  # Use `run` to ensure the command has the proper environment.
  /pkgfs/packages/run/0/bin/run $CMD &
fi

# For certain benchmarks (such as ones that depend on Flutter events), we need
# to sleep a bit here to let the Flutter processes start before we begin
# tracing, so that thread/process names will be present.  TODO(ZX-2706): This
# sleep can be removed once ZX-2706 is resolved.
if [ "${SLEEP_BEFORE_TRACE}" != '' ]; then
  echo "Sleep Before Trace: ${SLEEP_BEFORE_TRACE} seconds"
  sleep ${SLEEP_BEFORE_TRACE}
fi

# Only trace required events, which reduces trace size and prevents trace
# buffer overflow. TODO(ZX-1107): Overflow might be fixed with continuous
# tracing.
trace record --categories=$CATEGORIES --duration=$DURATION --buffer-size=$BUFFER_SIZE --output-file=$TRACE_FILE

echo "== $BENCHMARK_LABEL: Processing trace..."
/pkgfs/packages/scenic_benchmarks/0/bin/process_gfx_trace \
  -test_suite_name="${BENCHMARK_LABEL}" \
  -flutter_app_names="${FLUTTER_APP_NAMES}" \
  -all_apps="${ALL_APPS}" \
  -benchmarks_out_filename="${OUT_FILE}" \
  "${TRACE_FILE}"

echo "== $BENCHMARK_LABEL: Finished processing trace."

# Clean up so that we do not leave processes consuming CPU in the background.
kill_processes

echo "== $BENCHMARK_LABEL: Finished"
