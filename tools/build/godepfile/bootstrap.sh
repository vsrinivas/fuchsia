#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# found in the LICENSE file.

set -o errexit    # exit when a command fails
set -o nounset    # error when an undefined variable is referenced
set -o pipefail   # error if the input command to a pipe fails

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
readonly FUCHSIA_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

print_usage_and_exit() {
  readonly exit_code=$1
  # In the case of a non-zero exit, redirect the stdout below to stderr.
  if [[ $exit_code -ne 0 ]]; then
    exec 1>&2
  fi

  echo ""
  echo "Used to bootstrap the \"godepfile\" tool needed for building other go tools."
  echo ""
  echo "usage: $(basename "$0") -o <path> -a <GOARCH> -O <GOOS> -g <go_pkg_path> -b <go_bin> -p <GOPATH> -c <GOCACHE>"
  echo ""
  echo "options:"
  echo "-o: points to a file path at which to output godepfile; if unsupplied, it"
  echo "          will be output to the current working directory."
  echo "-a: specifies the GOARCH env setting (should be \"amd64\" or \"arm64\")."
  echo "-O: specifies the GOOS env setting (should be \"linux\" or \"darwin\")."
  echo "-g: points to the go package to build (should be relative to the fuchsia root directory)."
  echo "-b: points to the path of the go binary."
  echo "-p: specifies the GOPATH."
  echo "-c: specifies the GOCACHE."
  echo ""

  exit "$exit_code"
}

main() {
  local output
  output="$(pwd)/godepfile"

  while getopts 'ho:a:O:g:b:p:c:' opt; do
    case "$opt" in
      h) print_usage_and_exit 0 ;;
      o) output="${OPTARG}" ;;
      a) go_arch="${OPTARG}" ;;
      O) go_os="${OPTARG}" ;;
      g) go_pkg_path="${OPTARG}" ;;
      b) go_bin="${OPTARG}" ;;
      p) go_path="${OPTARG}" ;;
      c) go_cache="${OPTARG}" ;;
      ?) print_usage_and_exit 1  ;;
    esac
  done

  readonly GOPATH="$go_path"
  readonly GOCACHE="$go_cache"
  readonly GOOS="$go_os"
  readonly GOARCH="$go_arch"
  export GOPATH
  export GOCACHE
  export GOOS
  export GOARCH
  # Execute `go build` from the fuchsia root, as the package to build must be
  # supplied as a relative path.
  cd "${FUCHSIA_ROOT}" && ${go_bin} build -o "${output}" "./${go_pkg_path}"
}

main "$@"
