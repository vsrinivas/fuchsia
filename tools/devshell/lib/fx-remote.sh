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

function fetch_remote_build_archive {
  local host="$1"
  local dir="$2"

  ssh "${host}" "cd ${dir} && ./.jiri_root/bin/fx build build-archive.tar" || return 1

  local build_dir=$(ssh "${host}" "cd ${dir} && ./.jiri_root/bin/fx get-build-dir")
  if [[ -z "${build_dir}" ]]; then
    return 1
  fi

  rsync -z -P "${host}":"${build_dir}/build-archive.tar" "${FUCHSIA_DIR}/out/build-archive.tar"
  if [[ $? -ne 0 ]]; then
    return 1
  fi

  mkdir -p "${FUCHSIA_DIR}/out/fetched"
  tar xf "${FUCHSIA_DIR}/out/build-archive.tar" -C "${FUCHSIA_DIR}/out/fetched"
  if [[ $? -ne 0 ]]; then
    return 1
  fi
  echo >&2 "Build archive expanded into out/fetched"
}
