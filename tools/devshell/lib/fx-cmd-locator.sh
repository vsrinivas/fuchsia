#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function get_host_tools_dir {
  local -r build_dir="$(fx-build-dir-if-present && echo "${FUCHSIA_BUILD_DIR}")"
  local -r host_tools="${build_dir}/host-tools"
  if [[ -d "${build_dir}" && -d "${host_tools}"  ]]; then
    echo "${host_tools}"
  fi
}

function get_exec_from_metadata {
  local -r metadata_file=$1

  export PREBUILT_3P_DIR FUCHSIA_DIR HOST_PLATFORM
  export HOST_TOOLS_DIR="$(get_host_tools_dir)"

  awk -F ' *= *' -f - "${metadata_file}" <<'EOF'
    /^#### +EXECUTABLE */ {
      gsub(/\${PREBUILT_3P_DIR}/, ENVIRON["PREBUILT_3P_DIR"], $2);
      gsub(/\${FUCHSIA_DIR}/, ENVIRON["FUCHSIA_DIR"], $2);
      gsub(/\${HOST_TOOLS_DIR}/, ENVIRON["HOST_TOOLS_DIR"], $2);
      gsub(/\${HOST_PLATFORM}/, ENVIRON["HOST_PLATFORM"], $2);
      print $2;
    }
EOF
}

function _relative {
  cmd="$1"
  if [[ "${cmd}" == *"${FUCHSIA_DIR}"* ]]; then
    echo "//${cmd#${FUCHSIA_DIR}/}"
  else
    echo "${cmd}"
  fi
}

function find_executable {
  local cmd_name="$1"
  local cmd_path="$(commands "${cmd_name}")"
  # no metadata, so let's try to find the file
  if [[ -z "${cmd_path}" ]]; then
    # no file in regular script directories, look in host_tools
    cmd_path="$(find_host_tools "${cmd_name}")"
  fi
  # If multiple commands match, use the first one
  echo "$(find_exec_from_path "${cmd_path}" | head -1)"
}

function find_exec_from_path {
  local cmd_path="$1"
  cmd_path="${cmd_path%.fx}"
  local fx_file_path="${cmd_path}.fx"
  if [[ ! -f "${fx_file_path}" ]]; then
    # no metadata, so let's try to find the file
    echo "${cmd_path}"
    if [[ ! -x "${cmd_path}" ]]; then
      return 1
    fi
  else
    # there's metadata
    local from_metadata="$(get_exec_from_metadata "${fx_file_path}")"

    if [[ -n "${from_metadata}" ]]; then
      # EXECUTABLE is defined in the metadata, use it
      echo "${from_metadata}"
      if [[ -f "${cmd_path}"  && ! "${cmd_path}" -ef "${from_metadata}" ]]; then
        fx-error "Invalid ${fx_file_path}: if both ${basename_exec} and EXECUTABLE metadata exist, they must point to the same file"
        return 1
      fi
    else
      # no EXECUTABLE in the metadata, so return the cmd_path itself
      echo "${cmd_path}"
      if [[ ! -x "${cmd_path}" ]]; then
        return 1
      fi
    fi
  fi
}

function find_execs_and_metadata {
  local cmd_name=$1
  shift

  if [[ -z "${cmd_name}" ]]; then
    cmd_name="*"
  fi
  local dirs=()
  for d in "$@"; do
    if [[ -d "${d}" ]]; then
      dirs+=( "${d}" )
    fi
  done
  if [[ ${#dirs[@]} -eq 0 ]]; then
    return 0
  fi

  # run find assuming "-executable" is supported
  cmds="$( find "${dirs[@]}" -maxdepth 1 -type f \
    \( -executable -name "${cmd_name}" \) -o \
    \( -name "${cmd_name}.fx" \) 2>/dev/null )"

  if [[ $? -ne 0 ]]; then
    # assume that the error was caused because versions of find older than 4.3.0
    # don't support -executable. Run with -perm +100 instead, which is not
    # supported in versions of find newer than 4.5.12, so it can't be used always.
    cmds="$( find "${dirs[@]}" -maxdepth 1 -type f \
      \( -perm +100 -name "${cmd_name}" \) -o \
      \( -name "${cmd_name}.fx" \) 2>/dev/null )"
    if [[ $? -ne 0 ]]; then
      {
        echo "ERROR: 'find' failed unexpectedly, please execute fx with '-x' and report a bug."
        echo "At least one of the commands below was expected to work:"
        echo 'find ' "${dirs[@]}" '-maxdepth 1 -type f \( -executable -name' "\"${cmd_name}\"" '\) -o \( -name ' "\"${cmd_name}.fx\"" '\)'
        echo 'find ' "${dirs[@]}" '-maxdepth 1 -type f \( -perm +100 -name' "\"${cmd_name}\"" '\) -o \( -name ' "\"${cmd_name}.fx\"" '\)'
      } >&2
      exit 1
    fi
  fi
  sort -u <<< "${cmds}"
}


function find_host_tools {
  local cmd_name=$1
  local -r host_tools="$(get_host_tools_dir)"
  if [[ -z "${host_tools}" ]]; then
    return
  fi
  # get a list of non-host-tools commands and metadata files, separated by
  # semi-colon.
  cmds="$(commands | tr '\n' ';')"

  binaries=()
  # do not list a host tool if there's a subcommand script or an .fx metadata
  # file with the same name.
  for binary in $(find_execs_and_metadata "${cmd_name}" "${host_tools}"); do
    name="${binary##*/}"   # remove path, equivalent to basename but faster
    # only return the binary if no {binary} or {binary}.fx as other commands
    if [[ "${cmds}" != */${name}.fx\;* && "${cmds}" != */${name}\;* ]]; then
      binaries+=( "${binary}" )
    fi
  done
  echo "${binaries[@]}"
}


function commands {
  local cmd_name=${1:-}
  local dirs
  # handle "vendor VENDOR COMMAND"
  if [[ "${cmd_name}" == "vendor" && $# -eq 3 ]]; then
    vendor=$2
    cmd_name=$3
    dirs=("${FUCHSIA_DIR}"/vendor/${vendor}/scripts/devshell)
  # handle "vendor VENDOR"
  elif [[ "${cmd_name}" == "vendor" && $# -eq 2 ]]; then
    vendor=$2
    cmd_name=""
    dirs=("${FUCHSIA_DIR}"/vendor/${vendor}/scripts/devshell)
  else
    dirs=("${FUCHSIA_DIR}"/tools/devshell{,/contrib} "${FUCHSIA_DIR}"/vendor/*/scripts/devshell)
  fi

  find_execs_and_metadata "${cmd_name}" "${dirs[@]}"
}
