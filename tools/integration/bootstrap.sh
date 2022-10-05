#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -o errexit  # exit when a command fails
set -o nounset  # error when an undefined variable is referenced
set -o pipefail # error if the input command to a pipe fails

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
readonly FUCHSIA_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

print_usage_and_exit() {
  readonly exit_code=$1
  # In the case of a non-zero exit, redirect the stdout below to stderr.
  if [[ $exit_code -ne 0 ]]; then
    exec 1 >&2
  fi

  echo ""
  echo "Used to bootstrap the \"fint\" tool, which provides the platform's"
  echo '"integration interface".'
  echo "See //tools/integration/README.md for more details."
  echo ""
  echo "usage: $(basename "$0") (-o <path>)"
  echo ""
  echo "options:"
  echo "-o: points to a file path at which to output fint; if unsupplied, it"
  echo "    will be output to the current working directory."
  echo ""

  exit "$exit_code"
}

###############################################################################
# Returns the host platform, of the form <OS>-<architecture>.
# Globals:
#   None
# Arguments:
#   None
# Returns:
#   The host platform, if successful.
###############################################################################
host_platform() {
  readonly uname="$(uname -s -m)"
  case "${uname}" in
  "Linux x86_64") echo linux-x64 ;;
  "Darwin x86_64") echo mac-x64 ;;
  *)
    echo "unsupported infrastructure platform: ${uname}" 1>&2
    exit 1
    ;;
  esac
}

# The `realpath` command is not available on all systems, so we reimplement it
# here in pure bash. It converts relative paths to absolute, and leaves
# absolute paths as-is.
realpath() {
  [[ $1 == /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

main() {
  local output
  output="$(pwd)/fint"
  while getopts 'ho:' opt; do
    case "$opt" in
    h) print_usage_and_exit 0 ;;
    o) output=$(realpath "${OPTARG}") ;;
    ?) print_usage_and_exit 1 ;;
    esac
  done

  # Build in a temporary directory where we can arrange the module. Use `mktemp`
  # instead of directly referencing $TMPDIR or a similar environment variable
  # that may not be set on all platforms.
  BUILD_DIR=$(mktemp -d)
  trap 'rm -rf $BUILD_DIR' EXIT
  cd "$BUILD_DIR"
  for target in go.{mod,sum} vendor; do
    ln -s "$FUCHSIA_ROOT"/third_party/golibs/$target .
  done
  ln -s "$FUCHSIA_ROOT"/tools .

  GOCACHE_DIR=$(mktemp -d)
  trap 'rm -rf $GOCACHE_DIR' EXIT

  GOROOT_DIR="$FUCHSIA_ROOT/prebuilt/third_party/go/$(host_platform)"
  readonly go_bin="$GOROOT_DIR/bin/go"
  GOCACHE="$GOCACHE_DIR" GOROOT="$GOROOT_DIR" GOPROXY=off $go_bin build \
    -o "$output" ./tools/integration/fint/cmd/fint
}

main "$@"
