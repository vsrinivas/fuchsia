#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -o errexit    # exit when a command fails
set -o nounset    # error when an undefined variable is referenced
set -o pipefail   # error if the input command to a pipe fails

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
  echo "\"integration interface\"."
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
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

main() {
  local output
  output="$(pwd)/fint"
  while getopts 'ho:' opt; do
    case "$opt" in
      h) print_usage_and_exit 0 ;;
      o) output=$(realpath "${OPTARG}") ;;
      ?) print_usage_and_exit 1  ;;
    esac
  done

  # Ensure the go command runs from inside the module (which is rooted at the
  # repository root).
  readonly go_bin=prebuilt/third_party/go/$(host_platform)/bin/go
  cd "${FUCHSIA_ROOT}" && ${go_bin} build \
	  -mod=readonly -o "${output}" ./tools/integration/cmd/fint
}

main "$@"
