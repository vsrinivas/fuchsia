#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See usage() for description.

script="$0"
script_dir="$(dirname "$script")"

remote_action_wrapper="$script_dir"/fuchsia-rbe-action.sh

# The project_root must cover all inputs, prebuilt tools, and build outputs.
# This should point to $FUCHSIA_DIR for the Fuchsia project.
# ../../ because this script lives in build/rbe.
# The value is an absolute path.
project_root="$(readlink -f "$script_dir"/../..)"

dry_run=0
local_only=0
verbose=0
rewrapper_options=()

# Extract script options before --
for opt
do
  # handle --option arg
  if test -n "$prev_opt"
  then
    eval "$prev_opt"=\$opt
    prev_opt=
    shift
    continue
  fi
  # Extract optarg from --opt=optarg
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac
  case "$opt" in
    --help|-h) usage ; exit ;;
    --dry-run) dry_run=1 ;;
    --local) local_only=1 ;;
    # --fsatrace) trace=1 ;;
    --verbose|-v) verbose=1 ;;
    # --compare) compare=1 ;;
    # --check-determinism) check_determinism=1 ;;
    --project-root=*) project_root="$optarg" ;;
    --project-root) prev_opt=project_root ;;
    # stop option processing
    --) shift; break ;;
    # Forward all other options to rewrapper
    *) rewrapper_options+=( "$opt" ) ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

build_subdir="$(realpath --relative-to="$project_root" . )"
project_root_rel="$(realpath --relative-to=. "$project_root")"

cc=
cc_command=()

first_source=
output=
profile_list=

comma_remote_inputs=
comma_remote_outputs=


prev_opt=
for opt in "$@"
do
  # Copy most command tokens.
  dep_only_token="$opt"
  # handle --option arg
  if test -n "$prev_opt"
  then
    eval "$prev_opt"=\$opt
    case "$prev_opt" in
      remote_flag) rewrapper_options+=( "$opt" ) ;;
      comma_remote_inputs) ;;  # Remove this optarg.
      comma_remote_outputs) ;;  # Remove this optarg.
      # Copy all others.
      *) cc_command+=( "$opt" ) ;;
    esac
    prev_opt=
    shift
    continue
  fi

  # Extract optarg from --opt=optarg
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac

  case "$opt" in
    # This is equivalent to --local, but passed as a compiler flag,
    # instead of wrapper script flag (before the -- ).
    --remote-disable)
      local_only=1
      shift
      continue
      ;;

    # --remote-inputs signals to the remote action wrapper,
    # and not the actual compiler command.
    --remote-inputs=*)
      comma_remote_inputs="$optarg"
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;
    --remote-inputs)
      prev_opt=comma_remote_inputs
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;

    # --remote-outputs signals to the remote action wrapper,
    # and not the actual compiler command.
    --remote-outputs=*)
      comma_remote_outputs="$optarg"
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;
    --remote-outputs) prev_opt=comma_remote_outputs
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;

    # Redirect these flags to rewrapper.
    --remote-flag=*)
      rewrapper_options+=( "$optarg" )
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;
    --remote-flag) prev_opt=remote_flag
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;

    # This is the (likely prebuilt) cc binary.
    */clang* | */gcc* | */g++* ) cc="$opt" ;;

    -o) prev_opt=output ;;

    # TODO(b/220028444): integrate handling of this flag into reclient
    -fprofile-list=*) profile_list="$optarg" ;;
    -fprofile-list) prev_opt=profile_list ;;

    --*=* ) ;;  # forward

    # Forward other environment variables (or similar looking).
    *=*) ;;

    # Capture the first named source file as the source-root.
    *.cc | *.cpp | *.cxx | *.s | *.S)
        test -n "$first_source" || first_source="$opt"
        ;;

    *.a | *.o | *.so | *.so.debug)
        link_arg_files+=( "$build_subdir/$opt" )
        ;;

    # Preserve all other tokens.
    *) ;;
  esac

  # Copy tokens to craft a command for local and remote execution.
  cc_command+=( "$opt" )
  shift
done

case "$first_source" in
  # TODO(b/220030106): support remote preprocessing of assembly
  *.S) local_only=1 ;;
esac

if test "$local_only" = 1
then
  # TODO: add local file access tracing
  "${cc_command[@]}"
  exit "$?"
fi

# Specify the compiler binary to be uploaded.
cc_relative="$(realpath --relative-to="$project_root" "$cc")"

# Remove these temporary files on exit.
cleanup_files=()
function cleanup() {
  rm -f "${cleanup_files[@]}"
}
trap cleanup EXIT

# Collect extra inputs to upload for remote execution.
# Note: these paths are relative to the current working dir ($build_subdir),
# so they need to be adjusted relative to $project_root below, before passing
# them to rewrapper.
extra_inputs=()
test -z "$comma_remote_inputs" ||
  IFS=, read -ra extra_inputs <<< "$comma_remote_inputs"

# Collect extra outputs to download after remote execution.
extra_outputs=()
test -z "$comma_remote_outputs" ||
  IFS=, read -ra extra_outputs <<< "$comma_remote_outputs"


test -z "$profile_list" || {
  extra_inputs+=( "$profile_list" )
}

extra_inputs_rel_project_root=()
for f in "${extra_inputs[@]}"
do
  extra_inputs_rel_project_root+=( "$(realpath --relative-to="$project_root" "$f" )" )
done

remote_inputs=(
  "${extra_inputs_rel_project_root[@]}"
)

# List inputs in a file to avoid exceeding shell limit.
inputs_file_list="$output".inputs
mkdir -p "$(dirname "$inputs_file_list")"
(IFS=$'\n' ; echo "${remote_inputs[*]}") > "$inputs_file_list"
cleanup_files+=( "$inputs_file_list" )

# Outputs need to be adjusted relative to the exec_root.
# rewrapper already knows:
#   -o is followed by an output file.
#   -MF is followed by a depfile (an output).
remote_outputs=()
remote_outputs_joined=
test "${#extra_outputs[@]}" = 0 || {
  _remote_outputs_comma="$(printf "${build_subdir}/%s," "${extra_outputs[@]}")"
  remote_outputs_joined="${_remote_outputs_comma%,}"  # get rid of last trailing comma
}

remote_cc_command=(
  "$remote_action_wrapper"
  --labels=type=compile,compiler=clang,lang=cpp
  --exec_root="$project_root"
#  "${remote_trace_flags[@]}"
  --input_list_paths="$inputs_file_list"
  --output_files="$remote_outputs_joined"
  "${rewrapper_options[@]}"
  --
  "${cc_command[@]}"
)

# Cannot `exec` because that would bypass the trap cleanup.
"${remote_cc_command[@]}"
