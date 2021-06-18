#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See usage() for description.

script="$0"
script_dir="$(dirname "$script")"

# The project_root must cover all inputs, prebuilt tools, and build outputs.
# This should point to $FUCHSIA_DIR for the Fuchsia project.
# ../../ because this script lives in build/rbe.
project_root="$(readlink -f "$script_dir"/../..)"

function usage() {
cat <<EOF
This wrapper script helps dispatch a remote Rust compilation action by inferring
the necessary inputs for uploading based on the command-line.

$script [options] -- rustc-command...

Options:
  --help|-h: print this help and exit
  --local: disable remote execution and run the original command locally.
  --verbose|-v: print debug information, including details about uploads.
  --dry-run: print remote execution command without executing (remote only).

  --project-root: location of source tree which also encompasses outputs
      and prebuilt tools, forwarded to --exec-root in the reclient tools.
      [default: inferred based on location of this script]

  --source FILE: the Rust source root for the crate being built
      [default: inferred as the first .rs file in the rustc command]
  --depfile FILE: the dependency file that the command is expected to write.
      This file lists all source files needed for this crate.
      [default: this is inferred from --emit=dep-info=FILE]

Detected parameters:
  project_root: $project_root
EOF
}

local_only=0
dry_run=0
verbose=0

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
    --verbose|-v) verbose=1 ;;
    --project-root=*) project_root="$optarg" ;;
    --project-root) prev_opt=project_root ;;
    --source=*) top_source="$optarg" ;;
    --source) prev_opt=top_source ;;
    --depfile=*) depfile="$optarg" ;;
    --depfile) prev_opt=depfile ;;
    # stop option processing
    --) shift; break ;;
    *) echo "Unknown option: $opt"; usage; exit 1 ;;
  esac
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

# Copy the original command.
# Prefix with env, in case command starts with VAR=VALUE ...
rustc_command=(env "$@")

if test "$local_only" = 1
then
  # Run original command and exit (no remote execution).
  "${rustc_command[@]}"
  exit "$?"
fi

# Otherwise, prepare for remote execution.

# Modify original command to extract dep-info only (fast).
dep_only_command=()

# Infer the source_root.
first_source=

# C toolchain linker
linker=()
link_arg_files=()
link_sysroot=()

# input files referenced in environment variables
envvar_files=()

debug_var() {
  # With --verbose, prints variable values to stdout.
  # $1 is name of variable to display.
  # The rest are array values.
  test "$verbose" = 0 || {
    if test "$#" -le 2
    then
      echo "$1: $2"
    else
      echo "$1:"
      shift
      for f in "$@"
      do echo "  $f"
      done
    fi
  }
}

# Examine the rustc compile command
prev_opt=
for opt in "${rustc_command[@]}"
do
  # Copy most command tokens.
  dep_only_token="$opt"
  # handle --option arg
  if test -n "$prev_opt"
  then
    eval "$prev_opt"=\$opt
    prev_opt=
    dep_only_command+=( "$dep_only_token" )
    shift
    continue
  fi
  # Extract optarg from --opt=optarg
  case "$opt" in
    *=?*) optarg=$(expr "X$opt" : '[^=]*=\(.*\)') ;;
    *=) optarg= ;;
  esac

  case "$opt" in
    # This is the (likely prebuilt) rustc binary.
    */rustc) rustc="$opt" ;;

    # -o path/to/output.rlib
    -o) prev_opt=output ;;

    # Rewrite this token to only generate dependency information (locally),
    # and do no other compilation/linking.
    # Write to a renamed depfile because the remote command will also
    # produce the originally named depfile.
    --emit=*)
      test -n "$depfile" || {
        # Parse and split --emit=...,...
        IFS=, read -ra emit_args <<< "$optarg"
        for emit_arg in "${emit_args[@]}"
        do
          # Extract VALUE from dep-info=VALUE
          case "$emit_arg" in
            *=?*) emit_value=$(expr "X$emit_arg" : '[^=]*=\(.*\)') ;;
            *=) emit_value= ;;
          esac
          case "$emit_arg" in
            dep-info=*) depfile="$emit_value" ;;
          esac
        done
      }
      dep_only_token="--emit=dep-info=$depfile.nolink"
      # Tell rustc to report all transitive *library* dependencies,
      # not just the sources, because these all need to be uploaded.
      # This includes (prebuilt) system libraries as well.
      # TODO(fxb/78292): this -Z flag is not known to be stable yet.
      dep_only_command+=( "-Zbinary-dep-depinfo" )
      ;;

    # Detect custom linker, preserve symlinks
    -Clinker=*)
        linker=("$(realpath -s --relative-to="$project_root" "$optarg")")
        debug_var "[from -Clinker]" "${linker[@]}"
        ;;

    # sysroot is a directory with system libraries
    -Clink-arg=--sysroot=*)
        sysroot="$(expr "X$optarg" : '[^=]*=\(.*\)')"
        sysroot_relative="$(realpath --relative-to="$project_root" "$sysroot")"
        debug_var "[from -Clink-arg=--sysroot]" "$sysroot_relative"
        link_sysroot=("$sysroot_relative")
        ;;

    # Link arguments that reference .o or .a files need to be uploaded.
    -Clink-arg=*.o | -Clink-arg=*.a | -Clink-arg=*.so | -Clink-arg=*.so.debug)
        link_arg="$(realpath --relative-to="$project_root" "$optarg")"
        debug_var "[from -Clink-arg]" "$link_arg"
        link_arg_files+=("$link_arg")
        ;;

    # This flag informs the linker where to search for libraries.
    -Lnative=* )
        if test -d "$optarg"
        then
          # Rather than grab the entire directory (whose contents are not stable
          # due to temporary files being written during a build), list specific
          # shared objects and archives.  Observed temporary files include
          # .o.tmp files.
          #
          # Caveat: if the same dir is home to more than one archive produced by
          # parallel build actions, the partial directory listing may not be
          # stable throughout the entire build, and be subject to race
          # conditions.
          #
          # Some of these directories contain both .a and .o files.
          # It is not yet clear whether the .o files are needed when there is a
          # .a archive.
          # There are also directories that contain only .o files.
          # It is safe to over-specify inputs, so for now, we grab them all.
          #
          # || : to ignore exit code of ls.
          objs=($(ls "$optarg"/*.{so,a,o} 2> /dev/null)) || :
          objs_rel=($(echo "${objs[@]}" | grep . | xargs -n 1 realpath --relative-to="$project_root"))
          debug_var "[from -Lnative (dir:$optarg)]" "${objs_rel[@]}"
          link_arg_files+=("${objs_rel[@]}")
        else
          link_arg="$(realpath --relative-to="$project_root" "$optarg")"
          debug_var "[from -Lnative (file:$optarg)]" "$link_arg"
          link_arg_files+=("$link_arg")
        fi
        ;;

    --*=* ) ;;  # forward

    # Find files referenced in prefix environment variables.
    *=*)
        envvar=$(expr "X$opt" : '\([^=]*\)=.*')
        case "envvar" in
          # The following are used in src/lib/assembly/vbmeta/BUILD.gn:
          AVB_KEY) envvar_files+=("$optarg") ;;
          AVB_METADATA) envvar_files+=("$optarg") ;;
          EXPECTED_VBMETA) envvar_files+=("$optarg") ;;
          # from src/sys/pkg/bin/system-updater/BUILD.gn:
          EPOCH_PATH) envvar_files+=("$optarg") ;;
          # ignore all others
          *) ;;
        esac
        ;;

    # Capture the first named source file as the source-root.
    *.rs) test -n "$first_source" || first_source="$opt" ;;

    *.a | *.o | *.so | *.so.debug)
        link_arg_files+=("$(realpath --relative-to="$project_root" "$opt")")
        ;;

    # Preserve all other tokens.
    *) ;;
  esac
  # Copy tokens to craft a local command for dep-info.
  dep_only_command+=( "$dep_only_token" )
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

# Specify the rustc binary to be uploaded.
rustc_relative="$(realpath --relative-to="$project_root" "$rustc")"

# TODO(fangism): if possible, determine these shlibs statically to avoid `ldd`-ing.
# TODO(fangism): for host-independence, use llvm-otool and `llvm-readelf -d`,
#   which requires uploading more tools.
function nonsystem_shlibs() {
  # $1 is a binary
  ldd "$1" | grep "=>" | cut -d\  -f3 | \
    grep -v -e '^/lib' -e '^/usr/lib' | \
    xargs -n 1 realpath --relative-to="$project_root"
}

# The rustc binary might be linked against shared libraries.
# Exclude system libraries in /usr/lib and /lib.
# convert to paths relative to $project_root for rewrapper.
mapfile -t rustc_shlibs < <(nonsystem_shlibs "$rustc")

# At this time, the linker we pass is known to be statically linked itself
# and doesn't need to be accompanied by any shlibs.

# If --source was not specified, infer it from the command-line.
# This source file is likely to appear as the first input in the depfile.
test -n "$top_source" || top_source="$first_source"
top_source="$(realpath --relative-to="$project_root" "$top_source")"

# Locally generate a depfile only and read it as list of files to upload.
# These inputs appear relative to the build/output directory, but need to be
# relative to the $project_root for rewrapper.
"${dep_only_command[@]}"
mapfile -t depfile_inputs < <(grep ':$' "$depfile.nolink" | cut -d: -f1 | \
  xargs -n 1 realpath --relative-to="$project_root")
# Done with temporary depfile, remove it.
rm -f "$depfile.nolink"

# Inputs to upload include (all relative to $project_root):
#   * rust tool(s) [$rustc_relative]
#   * rust tool shared libraries [$rustc_shlibs]
#   * direct source files [$top_source]
#   * indirect source files [$depfile.nolink]
#   * direct dependent libraries [$depfile.nolink]
#   * transitive dependent libraries [$depfile.nolink]
#   * objects and libraries used as linker arguments [$link_arg_files]
#   * system rust libraries [$depfile.nolink]
#   * TODO(fangism): clang toolchain binaries for codegen and linking
#     For example: -Clinker=.../lld

# Need more than the bin/ directory, but its parent dir which contains tool
# libraries, and system libraries needed for linking.
# This is expected to cover the custom linker referenced by -Clinker=.
tools_dir=()
test "${#linker[@]}" = 0 || {
  tools_dir=("$(dirname "$(dirname "${linker[0]}")")")
}

remote_inputs=(
  "$rustc_relative"
  "${rustc_shlibs[@]}"
  "$top_source"
  "${depfile_inputs[@]}"
  "${envvar_files[@]}"
  "${tools_dir[@]}"
  "${link_arg_files[@]}"
  "${link_sysroot[@]}"
)
remote_inputs_joined="$(IFS=, ; echo "${remote_inputs[*]}")"

# Outputs include the declared output file and a depfile.
outputs=("$(realpath --relative-to="$project_root" "$output")")
test -z "$depfile" || outputs+=("$(realpath --relative-to="$project_root" "$depfile")")
# Removing outputs these avoids any unintended reuse of them.
rm -f "${outputs[@]}"
outputs_joined="$(IFS=, ; echo "${outputs[*]}")"

dump_vars() {
  debug_var "outputs" "${outputs[@]}"
  debug_var "rustc binary" "$rustc_relative"
  debug_var "rustc shlibs" "${rustc_shlibs[@]}"
  debug_var "source root" "$top_source"
  debug_var "linker" "${linker[@]}"
  debug_var "link args" "${link_arg_files[@]}"
  debug_var "link sysroot" "${link_sysroot[@]}"
  debug_var "env var files" "${envvar_files[@]}"
  debug_var "depfile" "$depfile"
  debug_var "[$script: dep-info]" "${dep_only_command[@]}"
  debug_var "depfile inputs" "${depfile_inputs[@]}"
  debug_var "tools dir" "${tools_dir[@]}"
}

dump_vars

# Assemble the remote execution command.
# During development, if you need to test a pre-release at top-of-tree,
# symlink the bazel-built binaries into a single directory, e.g.:
#  --bindir=$HOME/re-client/install-bin
# and pass options available in the new version, e.g.:
#  --preserve_symlink
remote_rustc_command=("$script_dir"/fuchsia-rbe-action.sh \
  --exec_root="$project_root" \
  --inputs="$remote_inputs_joined" \
  --output_files="$outputs_joined" -- \
  "${rustc_command[@]}")

# Execute the remote command.
if test "$dry_run" = 1
then
  echo "[$script: skipped]:" "${remote_rustc_command[@]}"
  dump_vars
else
  # Execute the rust command remotely.
  debug_var "[$script: remote]" "${remote_rustc_command[@]}"
  "${remote_rustc_command[@]}"
  status="$?"
  test "$status" = 0 || {
    # On any failure, dump debug info, even if it is not related to RBE.
    verbose=1
    cat <<EOF
======== Remote Rust build action FAILED ========
This could either be a failure with the original command, or something
wrong with the remote version of the command.
Try the command locally first, without RBE, and make sure that works.
Once it passes locally, re-enable RBE.
If the remote version still fails, file a bug, and CC the Fuchsia Build Team
with the following info below:

EOF
    # Identify which target failed by its command, useful in parallel build.
    debug_var "[$script: FAIL]" "${remote_rustc_command[@]}"
    dump_vars

    echo
    reproxy_errors=/tmp/reproxy.ERROR
    echo "The last lines of $reproxy_errors might explain a remote failure:"
    if test -r "$reproxy_errors" ; then tail "$reproxy_errors" ; fi
  }
  exit "$status"
fi
