# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -z "${TOOL_NAME}" ]]; then
  echo "This script is not intended to be run directly.";
  echo "Consider using one of the tool wrappers (e.g., gn, ninja) instead."
  exit 1
fi

case "$(uname -s)" in
  Darwin)
    readonly HOST_PLATFORM="mac"
    ;;
  Linux)
    readonly HOST_PLATFORM="linux64"
    ;;
  *)
    echo "Unknown operating system. Cannot run ${TOOL_NAME}."
    exit 1
    ;;
esac

readonly TOOL_PATH="${SCRIPT_ROOT}/${HOST_PLATFORM}/${TOOL_NAME}"

if [[ ! -x "${TOOL_PATH}" ]]; then
  echo "Cannot find ${TOOL_PATH}"
  echo "Did you run update.sh?"
  exit 1
fi

exec "${TOOL_PATH}" "$@"
