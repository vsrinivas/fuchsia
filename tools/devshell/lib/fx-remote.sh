#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# WARNING: This is not supposed to be directly executed by users.

readonly _REMOTE_INFO_CACHE_FILE=".fx-remote-config"

function load_remote_info {
  local current_host="$1"
  # if host is not given, check if there's cached info
  if [[ -z "${current_host}" && -f "${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE}" ]]; then
    cached="$(<${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE})"
    host=${cached%:*}
    dir=${cached#*:}
    {
      echo -n "Reusing host=${host}"
      if [[ -n "${dir}" ]]; then
        echo -n " and remote-path=${dir}"
      fi
      echo " from previous invocation, persisted in file //${_REMOTE_INFO_CACHE_FILE}"
    } >&2
    echo "$host" "$dir"
    return 0
  fi
  return 1
}

function save_remote_info {
  local host="$1"
  local dir="$2"
  echo "${host}:${dir}" > "${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE}"
}

