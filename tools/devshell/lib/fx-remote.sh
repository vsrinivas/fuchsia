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

function get_remote_build_dir {
  local host=$1
  local remote_checkout=$2
  echo $(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx get-build-dir")
}

function fetch_remote_build_artifacts {
  local host="$1"
  local remote_checkout="$2"
  local local_dir="$3"
  local mode="$4"  # See fx get-build-artifacts for possible values.
  local build="$5"

  local artifacts=($(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx get-build-artifacts --no-build ${mode}")) || exit $?

  if $build; then
    ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx build ${artifacts[@]}"
  fi

  local remote_build_dir="$(get_remote_build_dir "${host}" "${remote_checkout}")" || exit $?
  local joined=$(printf "%s," "${artifacts[@]}")
  mkdir -p "${local_dir}"
  # --relative ensures that the artifacts are copied to out/fetched relative to
  # the '/./' (i.e., the build directory).
  rsync --compress --partial --progress --relative "${host}:${remote_build_dir}/./{${joined}}" "${local_dir}" || exit $?
}

# Attempts to fetch a remote tool able to run on the host platform, and
# default to building one locally if unable to find one.
function fetch_or_build_tool {
  local host="$1"
  local remote_checkout="$2"
  local local_dir="$3"
  local tool_name="$4"

  local tool="$(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx get-build-artifacts --allow-empty --os ${HOST_OS} --cpu ${HOST_CPU}" --name ${tool_name} tools)"
  if [[ -n "${tool}" ]] ; then
    local remote_build_dir="$(get_remote_build_dir "${host}" "${remote_checkout}")" || exit $?
    rsync --compress --partial --progress --relative "${host}:${remote_build_dir}/./${tool}" "${local_dir}" >&2 || exit $?
  else
    tool="$(fx-command-run get-build-artifacts tools --expect-one --name ${tool_name})" || exit $?
    rm -f "${local_dir}/${tool}"
    mkdir -p "${local_dir}/$(dirname "$tool")"
    cp "${FUCHSIA_BUILD_DIR}/${tool}" "${local_dir}/${tool}"
  fi
  echo "${tool}"
}
