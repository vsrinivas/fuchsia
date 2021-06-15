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
    -Clinker=*) linker=("$(realpath -s --relative-to="$project_root" "$optarg")") ;;

    # Link arguments that reference .o or .a files need to be uploaded.
    -Clink-arg=*.o | -Clink-arg=*.a | -Clink-arg=*.so | -Clink-arg=*.so.debug)
        link_arg_files+=("$(realpath --relative-to="$project_root" "$optarg")")
        ;;

    -Clink-arg=--sysroot=*)
        sysroot="$(expr "X$optarg" : '[^=]*=\(.*\)')"
        link_sysroot=("$(realpath --relative-to="$project_root" "$sysroot")")
        ;;

    -Lnative=* )
        link_arg_files+=("$(realpath --relative-to="$project_root" "$optarg")")
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
test "$verbose" = 0 || echo "rustc binary: $rustc_relative"

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
test "$verbose" = 0 || {
  echo "rustc shlibs:"
  for f in "${rustc_shlibs[@]}"
  do echo "  $f"
  done
}

# At this time, the linker we pass is known to be statically linked itself
# and doesn't need to be accompanied by any shlibs.

# If --source was not specified, infer it from the command-line.
# This source file is likely to appear as the first input in the depfile.
test -n "$top_source" || top_source="$first_source"
top_source="$(realpath --relative-to="$project_root" "$top_source")"
test "$verbose" = 0 || echo "source root: $top_source"

test "$verbose" = 0 || test "${#linker[@]}" = 0 || echo "linker: ${linker[@]}"

test "$verbose" = 0 || test "${#link_arg_files[@]}" = 0 || {
  echo "link args:"
  for f in "${link_arg_files[@]}"
  do echo "  $f"
  done
}

test "$verbose" = 0 || test "${#link_sysroot[@]}" = 0 || echo "link sysroot: ${link_sysroot[@]}"

test "$verbose" = 0 || test "${#envvar_files[@]}" = 0 || {
  echo "env var files:"
  for f in "${envvar_files[@]}"
  do echo "  $f"
  done
}

test "$verbose" = 0 || echo "depfile: $depfile"

# Locally generate a depfile only and read it as list of files to upload.
# These inputs appear relative to the build/output directory, but need to be
# relative to the $project_root for rewrapper.
test "$verbose" = 0 || echo "[$script: dep-info]" "${dep_only_command[@]}"
"${dep_only_command[@]}"
mapfile -t depfile_inputs < <(grep ':$' "$depfile.nolink" | cut -d: -f1 | \
  xargs -n 1 realpath --relative-to="$project_root")
# Done with temporary depfile, remove it.
rm -f "$depfile.nolink"
test "$verbose" = 0 || {
  echo "depfile inputs: "
  for f in "${depfile_inputs[@]}"
  do echo "  $f"
  done
}

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
tools_dir="$(dirname "$(dirname "${linker[0]}")")"
test "$verbose" = 0 || echo "tools_dir: $tools_dir"

remote_inputs=(
  "$rustc_relative"
  "${rustc_shlibs[@]}"
  "$top_source"
  "${depfile_inputs[@]}"
  "${envvar_files[@]}"
  "$tools_dir"
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
test "$verbose" = 0 || {
  echo "outputs:"
  for f in "${outputs[@]}"
  do echo "  $f"
  done
}

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
else
  test "$verbose" = 0 || echo "[$script: remote]" "${remote_rustc_command[@]}"
  exec "${remote_rustc_command[@]}"
fi
