#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### register dev host as target's update source

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/vars.sh

function main {
  config_type="devhost"
  source_name=""
  local_hostname=""
  device_hostname=""
  while [[ $# -ne 0 ]]; do
    case "$1" in
      --type)
        config_type="$2"
        shift
        ;;
      --name)
        source_name="$2"
        shift
        ;;
      --local-hostname)
        local_hostname="$2"
        shift
        ;;
      --device-hostname)
        device_hostname="$2"
        shift
        ;;
      *)
        echo >&2 "Unrecognized option: $1"
        exit 1
    esac
    shift
  done

  if ! [[ -x "$(command -v shasum)" ]]; then
    echo >&2 "'shasum' binary not found"
    exit 1
  fi

  if [[ -z "${local_hostname}" ]]; then
    echo >&2 "Unable to determine host's IP."
    exit 1
  fi

  if [[ -z "${device_hostname}" ]]; then
    echo >&2 "Unable to determine device's IP.  Is the target up?"
    exit 1
  fi

  # Strip interface name suffix.
  local_hostname="${local_hostname%%%*}"

  repository_dir="${FUCHSIA_BUILD_DIR}/amber-files/repository"
  if [[ ! -d "${repository_dir}" ]]; then
    echo >&2 "Amber repository does not exist.  Please build first."
    exit 1
  fi

  config_dir="${repository_dir}/${source_name}"
  mkdir -p "${config_dir}"
  config_path="${config_dir}/config.json"
  config_url="http://[${local_hostname}]:8083/${source_name}/config.json"

  if [[ "${config_type}" == "devhost" ]]; then
    repo_url="http://[${local_hostname}]:8083"
  elif [[ "${config_type}" == "localhost" ]]; then
    repo_url="http://127.0.0.1:8083"
  else
    echo >&2 "Unknown config type. Valid options: devhost, localhost"
    exit 1
  fi

  "${FUCHSIA_DIR}/scripts/generate-update-source-config.py" \
    --build-dir="${FUCHSIA_BUILD_DIR}" \
    --name="${source_name}" \
    --repo-url="${repo_url}" \
    --blobs-url="${repo_url}/blobs" \
    --output "${config_path}" \
    || exit $?

  config_hash=$(shasum -a 256 $config_path | cut -f1 -d' ')

  "$TEST_LIB_DIR/ssh.sh" "$device_hostname" amber_ctl add_src \
    -f "${config_url}" \
    -h "${config_hash}"
  err=$?

  if [[ $err -ne 0 ]]; then
    echo >&2 "Unable to register update source."
    if [[ $err -eq 2 ]]; then
      # The GET request failed.
      echo >&2 " - Is 'fx serve' or 'fx serve-updates' running?"
      echo >&2 " - Can the target reach the development host on tcp port 8083?"
    fi
    exit 1
  fi
}

main "$@"
