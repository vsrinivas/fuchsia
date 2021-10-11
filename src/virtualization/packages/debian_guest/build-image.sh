#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -e

usage() {
  echo "usage: ${0} <output dir> (arm64|x64)"
  echo
  echo "Builds a Debian based image, initrd, and kernel suitable for Machina."
  echo
  exit 1
}

check_dep() {
  local bin="${1}"
  local package="${2:-${bin}}"
  type -P "${bin}" &>/dev/null && return 0

  echo "Required package ${package} is not installed. (sudo apt install ${package})"
  exit 1
}

main() {
  # Ensure correct number of args given.
  if [[ $# -ne 2 ]]; then
    usage
  fi

  # Create output directory.
  local output_dir
  output_dir=$(realpath "${1}")
  mkdir -p "${output_dir}" || exit 1

  # Target architecture.
  case "${2}" in
    arm64)
      local -r arch="arm64"
      ;;
    x86|x64)
      local -r arch="amd64"
      ;;
    *)
      usage;;
  esac

  # Ensure dependencies exist.
  check_dep debos
  check_dep zerofree
  check_dep qemu-img qemu-utils
  check_dep fstrim util-linux
  check_dep qemu-aarch64-static qemu-user-static

  # Create a temporary working directory.
  local working_dir
  working_dir=$(mktemp -d)
  echo "Working in ${working_dir}. Directory will be preserved on failure."

  # Get script's directory.
  local script_dir
  script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

  # Create the image.
  #
  # We change the working directory to ${working_dir} because debos
  # unhelpfully writes files to its current working directory.
  echo "Starting image creation VM..."
  (cd $working_dir;
    debos -v --artifactdir="${working_dir}" -t "arch:${arch}" "${script_dir}/debos/debos.yaml")

  # Move files to the output directory.
  for file in vmlinuz initrd.img rootfs.qcow2; do
      mv --force "${working_dir}/${file}" "${output_dir}/${file}"
  done

  # Remove working directory.
  rm -rf "${working_dir}"
}

main "$@"
