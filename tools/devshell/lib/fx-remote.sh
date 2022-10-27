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

    IFS=':' read -ra cached <<< "$(<${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE})"
    remote_host=${cached[0]}
    remote_dir=
    remote_os=

    if [[ "${#cached[@]}" -ge 2 ]]; then
      remote_dir=${cached[1]}
    fi
    if [[ "${#cached[@]}" -ge 3 ]]; then
      remote_os=${cached[2]}
    fi

    {
      echo -n "Reusing remote_host=${remote_host}"
      if [[ -n "${remote_dir}" ]]; then
        echo -n " and remote-path=${remote_dir}"
      fi
      if [[ -n "${remote_os}" ]]; then
        echo -n " and remote_os=${remote_os}"
      fi
      echo " from previous invocation, persisted in file //${_REMOTE_INFO_CACHE_FILE}"
    } >&2
    echo "$remote_host" "$remote_dir" "$remote_os"
    return 0
  fi
  return 1
}

function save_remote_info {
  local remote_host="$1"
  local remote_dir="$2"
  local remote_os
  remote_os=$(ssh "${host}" "uname") || ( \
    fx-error "Recieved error detecting remote host's operating system" && \
    exit $?)

  echo "${remote_host}:${remote_dir}:${remote_os}" > "${FUCHSIA_DIR}/${_REMOTE_INFO_CACHE_FILE}"
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
  mkdir -p "${local_dir}"

  # Artifacts are fetched in a loop since brace expansion occurs after variable
  # expansion on some setups, causing a combined rsync command to fail for some
  # users. Additionally, we cannot use multiple source arguments since this is
  # supported for rsync protocol version 30 onwards, which MacOS devices do not
  # ship with.
  for value in "${artifacts[@]}"
  do
    # --relative ensures that the artifacts are copied to out/fetched relative to
    # the '/./' (i.e., the build directory).
    # --times preserves the modification timestamp, which rsync uses in conjunction
    # with filesize to determine if the file changed (unless the --checksum
    # parameter is used). Without one of either --times or --checksum, identical
    # files will be re-transfered on each rsync invocation.
    rsync --compress --partial --progress --relative --times "${host}:${remote_build_dir}/./${value}" "${local_dir}" || exit $?
  done
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

  # If the remote OS does not equal the host OS, check if we have the tool
  # built locally and if so, just use the locally built version.
  local remote_os
  remote_os=$(ssh "${host}" "uname") || ( \
    fx-error "Recieved error detecting remote host's operating system" && \
    exit $?)
  local local_os
  local_os=$(uname)

  if [[ "${remote_os}" != "${local_os}" ]]; then
    if [[ -f "$(get_host_tools_dir)/${tool_name}" ]]; then
      fx-info "Using locally built ${tool_name}"
      echo "$(get_host_tools_dir)/${tool_name}"
      return
    fi
  fi

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
