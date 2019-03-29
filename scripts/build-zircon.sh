#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  echo "$0 <options> [<gn_build_arg>=<value> ...] [<ninja_arg> ...]"
  echo ""
  echo "Options:"
  echo "  -c: Clean before building"
  echo "  -g: Run GN but not Ninja"
  echo "  -G: Run Ninja but not GN (Ninja will re-run GN as needed)"
  echo "  -v: Level 1 verbosity"
  echo "  -V: Level 2 verbosity"
  echo "  -A: Build with ASan"
  echo "  -H: Build host tools with ASan"
  echo "  -n: Just print build commands to STDOUT."
  echo "  -T: Only build tools; do not build Zircon."
  echo "  -j N: Passed along to ninja (number of parallel jobs)"
  echo "  -l N: Passed along to ninja (maximum load average)"
  echo "  -t <target>: Architecture (GN style) to build, instead of all"
  echo "  -o <zircon-outdir>: Zircon build directory."
  echo "  -z <zircon-outdir>: TODO(BLD-383): DELETE. Deprecated alias for -o."
  echo ""
  echo 'Additional arguments containing `=` will be used as GN build arguments.'
  echo "Other additional arguments will be passed as extra Ninja arguments."
  echo "e.g., $0 use_goma=true core-tests-x64"
  echo ""
  echo 'Note that -A and -H translate into a `variants=...` build argument.'
  echo "You can't use those switches and also use such a build argument."
  echo "Note that if GN no build arguments (nor -c) are specified and the file"
  echo "<zircon-outdir>/args.gn already exists, it will be reused."
  echo "For nontrivial configuration changes, edit the args.gn file by hand"
  echo "either from scratch or after a run of this script with switches;"
  echo "then run this script with neither build arguments nor -A or -H."
}

readonly GN="${ROOT_DIR}/zircon/prebuilt/downloads/gn"
readonly NINJA="${ROOT_DIR}/zircon/prebuilt/downloads/ninja"

declare ASAN="false"
declare CLEAN="false"
declare DRY_RUN="false"
declare HOST_ASAN="false"
declare TOOLS_ONLY="false"
declare ZIRCON_BUILDROOT="${ROOT_DIR}/out/build-zircon"
declare VERBOSE="0"
declare -a ARCHLIST=(arm64 x64)
declare JOBS=0
declare LOADAVG=0
declare RUN_GN="true"
declare RUN_NINJA="true"

while getopts "AcgGHhl:nj:t:Tp:o:vVz:" opt; do
  case "${opt}" in
    A) ASAN="true" ;;
    c) CLEAN="true" ;;
    g) RUN_NINJA="false" ;;
    G) RUN_GN="false" ;;
    H) HOST_ASAN="true" ;;
    h) usage ; exit 0 ;;
    n) DRY_RUN="true" ;;
    j) JOBS="${OPTARG}" ;;
    l) LOADAVG="${OPTARG}" ;;
    o) ZIRCON_BUILDROOT="${OPTARG}" ;;
    t) ARCHLIST=("${OPTARG}") ;;
    T) TOOLS_ONLY="true" ;;
    v) VERBOSE="1" ;;
    V) VERBOSE="2" ;;
    z) echo >&2 "TODO(BLD-383): -z WILL BE DELETED REAL SOON."
       echo >&2 "Use -o instead."
       ZIRCON_BUILDROOT="${OPTARG}" ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done
shift $(($OPTIND - 1))

readonly ASAN CLEAN DRY_RUN HOST_ASAN PROJECTS VERBOSE ZIRCON_BUILDROOT
readonly -a ARCHLIST

if [[ "${CLEAN}" = "true" ]]; then
  rm -rf -- "${ZIRCON_BUILDROOT}"
fi

GN_CMD=("$GN" gen "$ZIRCON_BUILDROOT" --root="${ROOT_DIR}/zircon")
NINJA_CMD=("$NINJA" -C "$ZIRCON_BUILDROOT")
VARIANTS=()
GN_ARGS=()

if $ASAN; then
  VARIANTS+=(asan)
fi
if $HOST_ASAN; then
  VARIANTS+=(host_asan)
fi
if [ ${#VARIANTS[@]} -gt 0 ]; then
  VARIANTS_ARG='variants = ['
  for variant in "${VARIANTS[@]}"; do
    VARIANTS_ARG+="\"${variant}\", "
  done
  VARIANTS_ARG+=']'
  GN_ARGS+=("$VARIANTS_ARG")
fi

if [[ $VERBOSE -lt 1 ]]; then
  GN_CMD+=(-q)
  # Ninja doesn't have a --quiet switch.
fi
if [[ $VERBOSE -ge 2 ]]; then
  NINJA_CMD+=(-v)
fi

if $DRY_RUN; then
  NINJA_CMD+=(-n)
fi

if [[ "$JOBS" != 0 ]]; then
   NINJA_CMD+=(-j "$JOBS")
fi

if $TOOLS_ONLY; then
  NINJA_CMD+=(tools)
else
  NINJA_CMD+=(legacy-host_tests)
  for arch in "${ARCHLIST[@]}"; do
    NINJA_CMD+=("manifest-$arch")
  done
fi

for arg in "$@"; do
  # TODO(BLD-325): Special case for argument used by bot recipes.
  # Remove this after infra transitions.
  if [[ "$arg" == GOMACC=* ]]; then
    goma_dir="$(dirname "${arg#GOMACC=}")"
    GN_ARGS+=(use_goma=true "goma_dir=\"$goma_dir\"")
  elif [[ "$arg" == *=* ]]; then
    GN_ARGS+=("$arg")
  elif $RUN_NINJA; then
    NINJA_CMD+=("$arg")
  else
    echo >&2 "Extra Ninja arguments given when -g says not to run Ninja"
    exit 2
  fi
done

if [[ ${#GN_ARGS[@]} -gt 0 ]]; then
  if ! $RUN_GN; then
    echo >&2 "GN build arguments specified when -G says not to run GN"
    echo >&2 "Consider editting $ZIRCON_BUILDROOT/args.gn by hand instead."
    exit 2
  fi
  GN_CMD+=("--args=${GN_ARGS[*]}")
elif $RUN_GN && [[ $VERBOSE -gt 0 && -r "$ZIRCON_BUILDROOT/args.gn" ]]; then
  echo >&2 "Reusing existing $ZIRCON_BUILDROOT/args.gn file."
fi

run_cmd() {
  if [[ $VERBOSE -gt 0 ]]; then
    (set -x; "$@")
  else
    "$@"
  fi
}

if ! $RUN_GN && ! $RUN_NINJA; then
  echo >&2 "Doing nothing since -g and -G say not to."
  exit 0
fi

! $RUN_GN || run_cmd "${GN_CMD[@]}"
! $RUN_NINJA || run_cmd "${NINJA_CMD[@]}"
