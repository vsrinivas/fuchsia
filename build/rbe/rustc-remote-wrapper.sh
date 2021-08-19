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
# The value is an absolute path.
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

If the rust-command contains --remote-inputs=..., those will be interpreted
as extra --inputs to upload, and removed prior local and remote execution.
The option argument is a comma-separated list of files, relative to
\$project_root.

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

# Modify original command to extract dep-info only (fast).
# Start with `env` in case command starts with environment variables.
dep_only_command=(env)

# Infer the source_root.
first_source=

# C toolchain linker
linker=()
link_arg_files=()
link_sysroot=()

# input files referenced in environment variables
envvar_files=()

rust_lld=()

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
comma_remote_inputs=

# Compute a rustc command suitable for local and remote execution.
# Prefix command with `env` in case it starts with local environment variables.
rustc_command=(env)

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
      comma_remote_inputs) ;;  # Remove this optarg.
      # Copy all others.
      *) dep_only_command+=( "$dep_only_token" )
        rustc_command+=( "$opt" )
        ;;
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

  # Reject absolute paths, for the sake of build artifact portability,
  # and remote-action cache hit benefits.
  case "$opt" in
    *"$project_root"*)
      cat <<EOF
Absolute paths are not remote-portable.  Found:
  $opt
Please rewrite the command without absolute paths.
EOF
      exit 1
      ;;
  esac

  case "$opt" in
    # This is the (likely prebuilt) rustc binary.
    */rustc) rustc="$opt" ;;

    # --remote-inputs signals to the remote action wrapper,
    # and not the actual rustc command.
    --remote-inputs=*)
      comma_remote_inputs="$optarg"
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;
    --remote-inputs) prev_opt=comma_remote_inputs
      # Remove this from the actual command to be executed.
      shift
      continue
      ;;

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

    # --crate-type cdylib needs rust-lld (hard-coding this is a hack)
    cdylib)
      _rust_lld_rel="$(dirname "$rustc")"/../lib/rustlib/x86_64-unknown-linux-gnu/bin/rust-lld
      rust_lld=("$(realpath --relative-to="$project_root" "$_rust_lld_rel")")
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

    # Forward other environment variables (or similar looking).
    *=*) ;;

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
  # Copy tokens to craft a command for local and remote execution.
  rustc_command+=("$opt")
  shift
done
test -z "$prev_out" || { echo "Option is missing argument to set $prev_opt." ; exit 1;}

# Copy the original command.
# Prefix with env, in case command starts with VAR=VALUE ...

if test "$local_only" = 1
then
  # Run original command and exit (no remote execution).
  "${rustc_command[@]}"
  exit "$?"
fi

# Otherwise, prepare for remote execution.

# Specify the rustc binary to be uploaded.
rustc_relative="$(realpath --relative-to="$project_root" "$rustc")"

# Collect extra inputs to upload for remote execution.
extra_inputs=()
IFS=, read -ra extra_inputs <<< "$comma_remote_inputs"

# TODO(fangism): if possible, determine these shlibs statically to avoid `ldd`-ing.
# TODO(fangism): for host-independence, use llvm-otool and `llvm-readelf -d`,
#   which requires uploading more tools.
function nonsystem_shlibs() {
  # $1 is a binary
  ldd "$1" | grep "=>" | cut -d\  -f3 | \
    grep -v -e '^/lib' -e '^/usr/lib' | \
    xargs -n 1 realpath --relative-to="$project_root"
}

function depfile_inputs_by_line() {
  # From a depfile arrange deps one per line.
  # This looks at the phony deps "FILE:".
  grep ":$" "$1" | cut -d: -f1
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
"${dep_only_command[@]}" || {
  status=$?
  echo "Depfile generation failed.  Aborting."
  exit "$status"
}
mapfile -t depfile_inputs < <(depfile_inputs_by_line "$depfile.nolink" | \
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
#   * clang toolchain binaries for codegen and linking
#       For example: -Clinker=.../lld
#   * additional data dependencies [$extra_inputs]

# Need more than the bin/ directory, but its parent dir which contains tool
# libraries, and system libraries needed for linking.
# This is expected to cover the custom linker referenced by -Clinker=.
tools_dir=()
test "${#linker[@]}" = 0 || {
  tools_dir=("$(dirname "$(dirname "${linker[0]}")")")
}

remote_inputs=(
  "$rustc_relative"
  "${rust_lld[@]}"
  "${rustc_shlibs[@]}"
  "$top_source"
  "${depfile_inputs[@]}"
  "${envvar_files[@]}"
  "${tools_dir[@]}"
  "${link_arg_files[@]}"
  "${link_sysroot[@]}"
  "${extra_inputs[@]}"
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
  debug_var "rust lld" "${rust_lld[@]}"
  debug_var "source root" "$top_source"
  debug_var "linker" "${linker[@]}"
  debug_var "link args" "${link_arg_files[@]}"
  debug_var "link sysroot" "${link_sysroot[@]}"
  debug_var "env var files" "${envvar_files[@]}"
  debug_var "depfile" "$depfile"
  debug_var "[$script: dep-info]" "${dep_only_command[@]}"
  debug_var "depfile inputs" "${depfile_inputs[@]}"
  debug_var "tools dir" "${tools_dir[@]}"
  debug_var "extra inputs" "${extra_inputs[@]}"
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

if test "$dry_run" = 1
then
  echo "[$script: skipped]:" "${remote_rustc_command[@]}"
  dump_vars
  exit
fi

# Execute the rust command remotely.
debug_var "[$script: remote]" "${remote_rustc_command[@]}"
"${remote_rustc_command[@]}"
status="$?"

# Scan remote-generated depfile for absolute paths, and reject them.
abs_deps=()
if test -f "$depfile"
then
  # TEMPORARY WORKAROUND until upstream fix lands:
  #   https://github.com/pest-parser/pest/pull/522
  # Rewrite the depfile if it contains any absolute paths from the remote
  # build; paths should be relative to the root_build_dir.
  sed -i -e 's|/b/f/w/out/[^/]*/||g' "$depfile"

  mapfile -t remote_depfile_inputs < <(depfile_inputs_by_line "$depfile")
  for f in "${remote_depfile_inputs[@]}"
  do
    case "$f" in
      /*) abs_deps+=("$f") ;;
    esac
  done
  test "${#abs_deps[@]}" = 0 || status=1
  # error message below
fi

test "$status" = 0 || {
  # On any failure, dump debug info, even if it is not related to RBE.
  verbose=1
  cat <<EOF
======== Remote Rust build action FAILED ========
This could either be a failure with the original command, or something
wrong with the remote version of the command.

Try the command locally first, without RBE, and make sure that works.
Add 'disable_rbe = true' to the problematic Rust target in GN,
or 'fx set' without '--rbe' to disable globally.

Once it passes locally, re-enable RBE.
If the remote version still fails, file a bug, and CC the Fuchsia Build Team
with the following info below:

EOF
  # Identify which target failed by its command, useful in parallel build.
  debug_var "[$script: FAIL]" "${remote_rustc_command[@]}"
  dump_vars

  # Reject absolute paths in depfiles.
  if test "${#abs_deps[@]}" -ge 1
  then
    debug_var "Forbidden absolute paths in remote-generated depfile" "${abs_deps[@]}"
  fi
}

exit "$status"
