#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# WARNING: This is not supposed to be directly executed by users.

readonly _REMOTE_INFO_CACHE_FILE=".fx-remote-config"

function load_remote_info {
  local current_host="$1"
  # if remote_host is not given, check if there's cached info
  if [[ -z "${current_host}" && -f "${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE}" ]]; then
    cached="$(<${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE})"
    remote_host=${cached%:*}
    remote_dir=${cached#*:}
    {
      echo -n "Reusing remote_host=${remote_host}"
      if [[ -n "${remote_dir}" ]]; then
        echo -n " and remote-path=${remote_dir}"
      fi
      echo " from previous invocation, persisted in file //${_REMOTE_INFO_CACHE_FILE}"
    } >&2
    echo "$remote_host" "$remote_dir"
    return 0
  fi
  return 1
}

function save_remote_info {
  local remote_host="$1"
  local remote_dir="$2"
  echo "${remote_host}:${remote_dir}" > "${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE}"
}

# Return the name of the build output directory on the remote
function get_remote_build_dir {
  local host=$1
  local remote_checkout=$2
  echo $(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx get-build-dir")
}

# Search for a specific build artifact by name
function find_remote_build_artifact {
  local host="$1"
  local remote_checkout="$2"
  local name="$3"
  local mode="$4"

  echo $(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx list-build-artifacts --expect-one --name ${name} ${mode}")
}

# Fetch a named artifact from the remote build directory
function fetch_remote_artifacts {
  local host="$1"
  local remote_checkout="$2"
  local local_dir="$3"
  shift 3
  local artifacts=("$@")

  local remote_build_dir="$(get_remote_build_dir "${host}" "${remote_checkout}")" || exit $?
  local joined=$(printf "%s," "${artifacts[@]}")
  mkdir -p "${local_dir}"
  # --relative ensures that the artifacts are copied to out/fetched relative to
  # the '/./' (i.e., the build directory).
  # --times preserves the modification timestamp, which rsync uses in conjunction
  # with filesize to determine if the file changed (unless the --checksum
  # parameter is used). Without one of either --times or --checksum, identical
  # files will be re-transfered on each rsync invocation.
  rsync --compress --partial --progress --relative --times "${host}:${remote_build_dir}/./{${joined}}" "${local_dir}" || exit $?
}

# Fetch all build artifacts for a given mode, optionally building them first
function fetch_remote_build_artifacts {
  local host="$1"
  local remote_checkout="$2"
  local local_dir="$3"
  local mode="$4"  # See fx list-build-artifacts for possible values.
  local build="$5"

  local artifacts=($(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx list-build-artifacts ${mode}")) || exit $?

  if $build; then
    ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx build ${artifacts[@]}"
  fi

  fetch_remote_artifacts "${host}" "${remote_checkout}" "${local_dir}" "${artifacts[@]}"
}

# Attempts to fetch a remote tool able to run on the host platform, and
# default to building one locally if unable to find one.
function fetch_or_build_tool {
  local host="$1"
  local remote_checkout="$2"
  local local_dir="$3"
  local tool_name="$4"

  local tool="$(ssh "${host}" "cd ${remote_checkout} && .jiri_root/bin/fx list-build-artifacts --build --allow-empty --os ${HOST_OS} --cpu ${HOST_CPU}" --name ${tool_name} tools)"
  if [[ -n "${tool}" ]] ; then
    fetch_remote_artifacts "${host}" "${remote_checkout}" "${local_dir}" "${tool}" >&2
  else
    tool="$(fx-command-run list-build-artifacts --build --expect-one --name ${tool_name} tools)" || exit $?
    rm -f "${local_dir}/${tool}"
    mkdir -p "${local_dir}/$(dirname "$tool")"
    cp "${FUCHSIA_BUILD_DIR}/${tool}" "${local_dir}/${tool}"
  fi
  echo "${tool}"
}
