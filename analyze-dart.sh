#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs the Dart analyzer on a Dart build target.
#
# Usage: analyze_dart.sh path/to/target/directory
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

readonly PACKAGE="${1}"
readonly PACKAGE_NAME="$(basename "${PACKAGE}")"
readonly LIBRARY_DOT_PACKAGES="${OUT_DIR}/gen/${PACKAGE}/${PACKAGE_NAME}.packages"
readonly FLUTTER_DOT_PACKAGES="${OUT_DIR}/gen/${PACKAGE}/${PACKAGE_NAME}_dart_package.packages"
readonly SOURCES="${ROOT_DIR}/${PACKAGE}/lib/*.dart"

dot_packages="${LIBRARY_DOT_PACKAGES}"
if [[ ! -e "${dot_packages}" ]]; then
  dot_packages="${FLUTTER_DOT_PACKAGES}"
  if [[ ! -e "${dot_packages}" ]]; then
    echo "Could not find .packages file for ${PACKAGE_NAME}"
    exit 1
  fi
fi

${DART} --packages=${ANALYZER_PACKAGES} ${ANALYZER_MAIN} \
  --packages=${dot_packages} \
  ${SOURCES}
