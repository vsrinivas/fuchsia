#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs the Dart analyzer on a Dart build target.
#
# Note that this currently only works with targets whose name is the same as the
# directory they live in, and expects the source code to be in a subdirectory
# named lib.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
readonly OUT_DIR="${ROOT_DIR}/out/debug-x86-64"
readonly DART="${OUT_DIR}/host_x64/dart_no_observatory"
readonly ANALYZER_PACKAGES="${OUT_DIR}/host_x64/gen/dart/pkg/analyzer_cli/analyzer_cli.packages"
readonly ANALYZER_MAIN="${ROOT_DIR}/dart/pkg/analyzer_cli/bin/analyzer.dart"

function usage() {
  printf >&2 '%s: [-o options-file] -p package-root\n' "$0"
  printf >&2 'Notes:\n'
  printf >&2 '  Package root must be relative to the Fuchsia root\n'
  printf >&2 '  Options file may be a relative or absolute path\n'
  exit 1
}

function get_dot_packages() {
  local package="$1"
  local package_name="$(basename "${package}")"
  local library_dot_packages="${OUT_DIR}/gen/${package}/${package_name}.packages"
  if [[ -e "${library_dot_packages}" ]]; then
    echo "${library_dot_packages}"
    return
  fi
  local flutter_dot_packages="${OUT_DIR}/gen/${package}/${package_name}_dart_package.packages"
  if [[ -e "${flutter_dot_packages}" ]]; then
    echo "${flutter_dot_packages}"
    return
  fi
  printf >&2 'Could not find .packages file for %s\n' "${package}"
}

function get_sources() {
  local package="$1"
  echo "${ROOT_DIR}/${package}/lib/*.dart"
}

declare options_flag=""
declare package_arg=""
while getopts "o:p:h" opt; do
  case "${opt}" in
    o) options_flag="--options=${OPTARG}" ;;
    p) package_arg="${OPTARG}" ;;
    *) usage ;;
  esac
done
readonly options_flag

if [[ -z "${package_arg}" ]]; then
  usage
fi
readonly dot_packages=$(get_dot_packages "${package_arg}")
if [[ -z "${dot_packages}" ]]; then
  exit 1
fi
readonly sources=$(get_sources "${package_arg}")

${DART} --packages=${ANALYZER_PACKAGES} ${ANALYZER_MAIN} \
  --packages=${dot_packages} \
  ${options_flag} \
  ${sources}
