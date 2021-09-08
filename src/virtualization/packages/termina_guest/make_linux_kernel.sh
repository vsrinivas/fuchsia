#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} (-l <linux-dir> | -b <branch>) [options] (arm64 | x64 )"
  echo
  echo "  -l <linux-dir>  Build Linux from an existing checkout from this"
  echo "                  directory. (default: /tmp/linux)"
  echo "  -b <branch>     Clone or fetch this branch of the Linux kernel from "
  echo "                  http://zircon-guest.googlesource.com/."
  echo "                  Linux will be cloned into <linux-dir> if specified."
  echo "  -d <defconfig>  Linux config to use (default: machina_defconfig)."
  echo "  -o <image>      Output location for the built kernel."
  echo
  exit 1
}

while getopts "b:d:l:o:" OPT; do
  case $OPT in
  b) LINUX_BRANCH="${OPTARG}";;
  d) LINUX_DEFCONFIG="${OPTARG}";;
  l) LINUX_DIR="${OPTARG}";;
  o) LINUX_OUT="${OPTARG}";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

# Ensure either "-b <branch>" or "-l <linux-dir>" were provided.
if [[ -z "${LINUX_BRANCH}" && -z ${LINUX_DIR} ]]; then
  echo "Error: Either one of '-b <branch>' or '-l <linux dir>' must be provided."
  echo ""
  usage
fi

declare -r LINUX_DIR=${LINUX_DIR:-/tmp/linux}
declare -r LINUX_DEFCONFIG=${LINUX_DEFCONFIG:-machina_defconfig}

# Configure environment for the specified architecture.
case "${1}" in
arm64)
  type aarch64-linux-gnu-gcc-9 ||
    { echo "Required package gcc-aarch64-linux-gnu-9 is not installed."
      echo "(sudo apt install gcc-aarch64-linux-gnu-9)"; exit 1; }

  declare -rx ARCH=arm64
  declare -x CROSS_COMPILE=aarch64-linux-gnu-
  declare -r LINUX_IMAGE="${LINUX_DIR}/arch/arm64/boot/Image";;
x64)
  declare -rx ARCH=x86_64
  declare -x CROSS_COMPILE=x86_64-linux-gnu-
  declare -r LINUX_IMAGE="${LINUX_DIR}/arch/x86/boot/bzImage";;
"")
  echo "Error: No architecture specified."
  echo ""
  usage;;
*)
  echo "Error: Unknown architecture specified."
  echo ""
  usage;;
esac

# Set compiler to use for Linux compile.
#
# Linux 4.18 is not compatible with gcc-10. Linux 5.4 is compatible with both
# gcc-9 and gcc-10. We default to using gcc-9 while we still need to support
# Linux 4.18.
declare -x CC=${CROSS_COMPILE}gcc-9

# Clone and checkout the given branch.
if [ -n "${LINUX_BRANCH}" ]; then
  # Shallow clone the repository if it doesn't exist.
  if [ ! -d "${LINUX_DIR}/.git" ]; then
    echo "Cloning Linux into ${LINUX_DIR}..."
    git clone --depth 1 --branch "${LINUX_BRANCH}" https://zircon-guest.googlesource.com/third_party/linux "${LINUX_DIR}"
  fi

  # Ensure the repository is clean.
  if ! git -C "${LINUX_DIR}" diff --quiet; then
    echo "Git repository in ${LINUX_DIR} is unclean. Aborting."
    exit 1
  fi

  # Update the repository.
  echo "Updating and checking out branch ${LINUX_BRANCH}..."
  pushd "${LINUX_DIR}"
  if [[ $(git branch --list "${LINUX_BRANCH}") ]]; then
    git checkout "${LINUX_BRANCH}"
    git pull --depth 1 origin "${LINUX_BRANCH}"
  else
    git fetch --depth 1 origin "${LINUX_BRANCH}:${LINUX_BRANCH}"
    git checkout "${LINUX_BRANCH}"
  fi
  popd
fi

# Build Linux.
(
    cd "${LINUX_DIR}"

    # Avoid putting the current user's username/hostname in the output image.
    declare -rx KBUILD_BUILD_USER="machina"
    declare -rx KBUILD_BUILD_HOST="fuchsia.com"

    # Avoid embedding the user's timezone in the output image.
    declare -rx TZ=UTC

    # CC must be specified on the command line to override the setting in Makefile
    make CC=${CC} "${LINUX_DEFCONFIG}"
    make CC=${CC} -j "$(getconf _NPROCESSORS_ONLN)"
)

# Copy output image to destination.
if [ -n "${LINUX_OUT}" ]; then
  mkdir -p "$(dirname "${LINUX_OUT}")"
  cp "${LINUX_IMAGE}" "${LINUX_OUT}"
fi
