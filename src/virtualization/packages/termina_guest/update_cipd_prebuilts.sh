#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -o pipefail

declare -r TERMINA_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR="${TERMINA_GUEST_DIR}/../../../../"

# The revision of the prebuilt to fetch. The latest build revision can be read
# from:
#
# https://storage.googleapis.com/chromeos-image-archive/{tatl|tael}-full/LATEST-master
declare -r TERMINA_REVISION=R76-12182.0.0-rc1

print_usage_and_exit() {
  echo "Update prebuilt termina images from published Chrome images."
  echo ""
  echo "This script assists in updating the CIPD prebuilt images of the termina"
  echo "VM by downloading prebuilts from the chrome image archive and converting"
  echo "them into a format that is compatible with Fuchsia."
  echo ""
  echo "Note: This script requires a valid ChromeOS source tree. Instructions"
  echo "for setting that up can be found at:"
  echo "    https://sites.google.com/a/chromium.org/dev/chromium-os/quick-start-guide"
  echo "and it's recommended to run the following inside cros_sdk prior to"
  echo "using this script:"
  echo "    ./setup_board --board=tatl"
  echo "    ./build_packages --board=tatl"
  echo "    ./build_image --board=tatl test"
  echo ""
  echo "Usage:"
  echo "  update_cipd_prebuilts.sh -c [CROS_WORKSPACE_PATH] -r [TERMINA_REVISION]"
  echo "      -k [KERNEL_OVERLAY_DIR] [-n] [-f]"
  echo ""
  echo "Where:"
  echo "   -c [CROS_WORKSPACE_PATH] - path to a ChromeOS work tree. Instructions"
  echo "      for setting that up can be found at:"
  echo "      https://sites.google.com/a/chromium.org/dev/chromium-os/quick-start-guide"
  echo ""
  echo "   -r [TERMINA_REVISION] - Version of termina to publish to CIPD. This will"
  echo "      be a string that looks something like 'R76-12182.0.0-rc1'. If omitted,"
  echo "      the most recent version will be used."
  echo ""
  echo "   -k [KERNEL_OVERLAY_DIR] - Directory to use for kernel overlays. This should"
  echo "      be a directory with the following layout:"
  echo ""
  echo "      KERNEL_OVERLAY_DIR/"
  echo "         |--x64"
  echo "         |    |-vm_kernel"
  echo "         |--arm64"
  echo "         |    |-vm_kernel"
  echo ""
  echo "      These kernels will be used in lieu of the prebuilt kernels from the"
  echo "      Chrome image archive. If the kernel does not exist in the overlay, "
  echo "      then the Chrome prebuilt will still be used."
  echo ""
  echo "   -n - Dry run. Don't actually upload anything, just show what would be"
  echo "      uploaded."
  echo ""
  echo "   -f - Force. Script will refuse to upload unless this flag is specified."

  exit $1
}

check_depot_tools() {
  type -P cros_sdk &>/dev/null && return 0
  echo "ChromeOS depot_tools not found"
  echo ""
  echo "This script requires a ChromeOS workspace to run. For setup instructions,"
  echo "see:"
  echo "  https://sites.google.com/a/chromium.org/dev/chromium-os/quick-start-guide"
  exit 1
}

board_for_arch() {
  case "${1}" in
    arm64)
      echo tael-full ;;
    x64)
      echo tatl-full ;;
    *)
      >&2 echo "Unsupported arch ${1}; should be one of x64, arm64";
      exit -1;;
  esac
}

# Downloads and decompresses a prebuilt termina image.
#
# $1 - Architecture (x64 or arm64).
# $2 - Termina revision to use. To find the latest master build see
#      https://storage.googleapis.com/chromeos-image-archive/tatl-full/LATEST-master
#      or
#      https://storage.googleapis.com/chromeos-image-archive/tael-full/LATEST-master
# $3 - Directory to decompress the prebuilt image into.
#
# The images distributed by Chrome are full disk images that contains several
# partitions. These images are not directly bootable on Fuchsia and will require
# some post-processing to be usable.
fetch_and_decompress() {
  local -r arch="$1"
  local -r revision="$2"
  local -r outdir="$3"

  local -r board="`board_for_arch ${arch}`"
  local -r artifact_dir="${outdir}/${board}"

  mkdir -p "${artifact_dir}"
  wget -O "${artifact_dir}/chromiumos_base_image.tar.xz" \
    "https://storage.googleapis.com/chromeos-image-archive/${board}/${revision}/chromiumos_base_image.tar.xz"
  tar xvf "${artifact_dir}/chromiumos_base_image.tar.xz" -C "${artifact_dir}"
  rm "${artifact_dir}/chromiumos_base_image.tar.xz"
}

# Extracts a kernel image and rootfs filesystem image from the bootable disk
# image provided by ChromeOS.
#
# $1 - Architecture (x64 or arm64).
# $2 - Path to a ChromeOS workspace. This is needed as we'll reuse the scripts
#      provided by Chrome to extract the kernel and rootfs from the full disk
#      image.
termina_build_image() {
  local -r arch="$1"
  local -r cros_dir="$2"

  local -r board="`board_for_arch ${arch}`"
  # Note that the references to $HOME here will be resoved inside the ChromeOS
  # chroot and not the current users $HOME. This is because the ChromeOS build
  # system relies on doing a chroot into a sysroot to support the build. This
  # is handled by the 'cros_sdk' command below.
  local -r chroot_outdir="~/${board}/output"
  local -r input_image="~/${board}/chromiumos_base_image.bin"

  pushd "${cros_dir}"
  cros_sdk bash -c "rm -rf ${chroot_outdir} && ~/trunk/src/platform/container-guest-tools/termina/termina_build_image.py ${input_image} ${chroot_outdir}"
  popd
}

# Returns the most recent revision for a given board.
#
# $1 - Board requested.
latest_revision_for_board() {
  local -r board="$1"
  curl -s https://storage.googleapis.com/chromeos-image-archive/${board}/LATEST-master
}

main() {
  while getopts "c:r:k:nfh" FLAG; do
    case "${FLAG}" in
    c) cros_dir="${OPTARG}" ;;
    r) termina_revision_requested="${OPTARG}" ;;
    k) kernel_overlay_dir="${OPTARG}" ;;
    n) dry_run=true ;;
    f) force=true ;;
    h) print_usage_and_exit 0 ;;
    *) print_usage_and_exit 1 ;;
    esac
  done
  shift $((OPTIND - 1))

  declare -r cipd="${FUCHSIA_DIR}/.jiri_root/bin/cipd"
  declare -r cros_dir=${cros_dir}
  declare -r termina_revision_requested=${termina_revision_requested}
  declare -r kernel_overlay_dir=${kernel_overlay_dir}
  declare -r dry_run=${dry_run}
  declare -r force=${force}
  declare jiri_entries="    <!-- termina guest images -->"

  if [[ -z "${cros_dir}" ]];
  then
    print_usage_and_exit 1
  fi

  if [ "$dry_run" == "$force" ];
  then
    print_usage_and_exit 1
  fi

  check_depot_tools

  for arch in "x64" "arm64"
  do
    board=`board_for_arch ${arch}`
    termina_revision=${termina_revision_requested:-`latest_revision_for_board ${board}`}
    kernel_overlay_path="${kernel_overlay_dir}/${arch}/vm_kernel"
    if [[ ! -z "${kernel_overlay_dir}" ]] && [[ -f "${kernel_overlay_path}" ]]; then
      echo "Using kernel from overlay ${kernel_overlay_path}"
      kernel_override="${kernel_overlay_path}"
    fi

    jiri_entries="${jiri_entries}
    <package name=\"fuchsia_internal/linux/termina-${arch}\"
             version=\"termina-rev:${termina_revision}\"
             path=\"prebuilt/virtualization/packages/termina_guest/images/${arch}\"
             internal=\"true\"/>"

    # See if we've already uploaded this artifact to avoid having multiple
    # CIPD instances with the same tag.
    ${cipd} \
      search fuchsia_internal/linux/termina-${arch} \
      -tag "termina-rev:${termina_revision}" | grep "fuchsia_internal/linux/termina-${arch}"
    if [[ $? -eq 0 ]];
    then
      echo "CIPD artifact already exists for ${termina_revision} on arch ${arch}; skipping upload."
      continue
    fi

    fetch_and_decompress "${arch}" "${termina_revision}" "${cros_dir}/chroot/home/${USER}"
    termina_build_image "${arch}" "${cros_dir}"

    if [[ ! -z "${kernel_override}" ]]; then
      cp "${kernel_override}" "${cros_dir}/chroot/home/${USER}/${board}/output/vm_kernel"
    fi

    options=()
    options+=("create")
    options+=("-in" "${cros_dir}/chroot/home/${USER}/${board}/output")
    options+=("-name" "fuchsia_internal/linux/termina-${arch}")
    options+=("-install-mode" "copy")
    options+=("-tag" "termina-rev:${termina_revision}")

    echo cipd ${options[*]}
    if [ "$dry_run" != true ] ; then
      ${cipd} ${options[*]}
    fi
  done

  echo "Update //integration/fuchsia/prebuilts with the following:"
  echo "${jiri_entries}"
}

main "$@"
