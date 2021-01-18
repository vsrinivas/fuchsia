#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Find all BUILD.zircon.gn files that have a matching BUILD.gn symlink
# under //zircon, then:
#   - Remove the symlink
#   - Rename the file to BUILD.gn
#
# Also remove any remaining BUILD.zircon.gn without a matching BUILD.gn
# after this since they are no longer needed.
#
# After running this scripts, a few BUILD.zircon.gn files will remain,
# all with a non-symlink BUILD.gn file in the same directory.

set -e

# Find all BUILD.zircon.gn that don't have a corresponding BUILD.gn file.
ZN_FILES="$(find zircon -name "BUILD.zircon.gn" -type f)"
WITH_GN_FILES=()
NO_GN_FILES=()

for ZN_FILE in ${ZN_FILES}; do
  GN_FILE="${ZN_FILE%.zircon.gn}.gn"
  if ! [[ -e "${GN_FILE}" ]]; then
    NO_GN_FILES+=("${ZN_FILE}")
  else
    if [[ -h "${GN_FILE}" ]]; then
      TARGET="$(readlink "${GN_FILE}")"
      if [[ "${TARGET}" != "BUILD.zircon.gn" ]]; then
        echo >&2 "ERROR: ${GN_FILE} is a symlink that does not point to ${ZN_FILE}"
      fi
      WITH_GN_FILE+=("${ZN_FILE}")
    fi
  fi
done

for ZN_FILE in ${WITH_GN_FILE[@]}; do
  GN_FILE="${ZN_FILE%.zircon.gn}.gn"
  echo "Reset ${GN_FILE} from ${ZN_FILE}"
  git mv -f "${ZN_FILE}" "${GN_FILE}"
done

for ZN_FILE in ${NO_GN_FILES[@]}; do
  echo "Remove ${ZN_FILE}"
  git rm "${ZN_FILE}"
done

