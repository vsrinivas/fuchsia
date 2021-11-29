#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -o pipefail

# exit when any command fails
set -e

declare -r TERMINA_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR="${TERMINA_GUEST_DIR}/../../../../"

# The revision of Termina to build. The latest build revision can be read
# from:
#
# https://storage.googleapis.com/chromeos-image-archive/{tatl|tael}-full/LATEST-master
declare -r TERMINA_REVISION=R90-13816.B

print_usage_and_exit() {
  echo "Build Termina images for Machina."
  echo ""
  echo "Usage:"
  echo "  update_cipd_prebuilts.sh work_dir (x64|arm64) [-n] [-f]"
  echo ""
  echo "Where:"
  echo "      work_dir - An empty directory to place source trees and build artifacts."
  echo "      Note: script may generate contents of ~125 GB"
  echo ""
  echo "   -n Dry run. Don't actually upload anything, just show what would be"
  echo "      uploaded."
  echo ""
  echo "   -f Force. Script will refuse to upload unless this flag is specified."

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
      echo tael ;;
    x64)
      echo tatl ;;
    *)
      >&2 echo "Unsupported arch ${1}; should be one of x64, arm64";
      exit -1;;
  esac
}

create_cros_tree() {
  local -r cros_dir="$1"
  local -r termina_revision="$2"

  mkdir -p "${cros_dir}"
  pushd "${cros_dir}"
  repo init -u https://chrome-internal.googlesource.com/chromeos/manifest-internal -b release-"${termina_revision}"
  repo sync -j8

  (cd src/overlays; git checkout cros/test-magma)
  (cd src/third_party/chromiumos-overlay; git checkout cros/test-magma)
  (cd src/platform2; git checkout cros/test-magma)
  (cd src/platform/minigbm; git checkout cros/test-magma)

  # Apply patches to minigbm temporarily
  (cd src/platform/minigbm; git fetch https://chromium.googlesource.com/chromiumos/platform/minigbm refs/changes/72/3276672/13 && git cherry-pick FETCH_HEAD)
  (cd src/platform/minigbm; git fetch https://chromium.googlesource.com/chromiumos/platform/minigbm refs/changes/36/3276636/4 && git cherry-pick FETCH_HEAD)
  (cd src/platform/minigbm; git fetch https://chromium.googlesource.com/chromiumos/platform/minigbm refs/changes/88/3276688/4 && git cherry-pick FETCH_HEAD)

  # Switch to Fuchsia's mesa branch
  (cd src/third_party/mesa && \
    git config remote.fuchsia.url >&- || \
    git remote add fuchsia https://fuchsia.googlesource.com/third_party/mesa && \
    git remote update fuchsia && \
    git checkout fuchsia/main)

  if [ ! -d src/third_party/magma ]; then
    # Create magma area and build it from fuchsia
    mkdir -p src/third_party/magma
    (cd src/third_party/magma && "${FUCHSIA_DIR}/src/graphics/lib/magma/scripts/install-magma-linux.sh")
  fi

  popd
}

build_angle() {
  local -r angle_dir="$1"
  local -r arch="$2"

  [ ! -d "${angle_dir}" ] && git clone https://chromium.googlesource.com/angle/angle "${angle_dir}"
  pushd "${angle_dir}"

  python3 scripts/bootstrap.py
  gclient sync
  build/linux/sysroot_scripts/install-sysroot.py --arch=${arch}

  gn gen out --args="\
    is_debug=false \
    target_cpu=\"${arch}\" \
    target_os=\"linux\" \
    use_x11=true \
    use_ozone=true \
    angle_enable_trace=false \
    angle_enable_d3d9=false \
    angle_enable_d3d11=false \
    angle_enable_gl=false \
    angle_enable_metal=false \
    angle_enable_null=false \
    angle_enable_vulkan=true \
    angle_enable_essl=false \
    angle_enable_glsl=false \
    angle_build_all=true \
    angle_enable_swiftshader=false \
    angle_use_custom_libvulkan=false \
    angle_egl_extension=\"so.1\" \
    angle_glesv2_extension=\"so.2\""

  ninja -C out angle gles2_torus_lighting

  popd
}

# Builds the Termina image, for details see:
# https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/main/project-termina/#building
#
# $1 - Architecture (x64 or arm64).
# $2 - Path to a ChromeOS workspace.
build_termina_image() {
  local -r arch="$1"
  local -r cros_dir="$2"

  local -r board="`board_for_arch ${arch}`"
  # Note that the references to $HOME here will be resoved inside the ChromeOS
  # chroot and not the current users $HOME. This is because the ChromeOS build
  # system relies on doing a chroot into a sysroot to support the build. This
  # is handled by the 'cros_sdk' command below.
  local -r chroot_outdir="~/termina-${board}-image"
  local -r input_image="~/trunk/src/build/images/${board}/latest/chromiumos_image.bin"

  pushd "${cros_dir}"

  cros_sdk bash -c "setup_board --board=${board}"

  # Switch to source build for mesa
  cros_sdk bash -c "cros_workon --board=${board} start mesa"

  # Switch to source build of sommelier
  cros_sdk bash -c "cros_workon --board=${board} start sommelier"

  # Switch to source build of magma
  cros_sdk bash -c "cros_workon --board=${board} start magma"

  # Switch to source build of minigbm
  cros_sdk bash -c "cros_workon --board=${board} start minigbm"

  # Build chromeos image
  cros_sdk bash -c "./build_packages --board=${board} --nowithautotest && \
    ./build_image --board=${board} --noenable_rootfs_verification"

  # Extract Termina from chromeos image
  cros_sdk bash -c "sudo rm -rf ${chroot_outdir} && \
    sudo ~/trunk/src/platform/container-guest-tools/termina/termina_build_image.py ${input_image} ${chroot_outdir}"

  popd
}

main() {
  if [[ $# < 2 ]]; then
    print_usage_and_exit
  fi

  work_dir="$1"
  shift

  arch="$1"
  shift

  if [[ "${arch}" != "x64" && "${arch}" != "arm64" ]]; then
    echo "Expected x64 or arm64, got: ${arch}"
    print_usage_and_exit
  fi

  while getopts "nfh" FLAG; do
    case "${FLAG}" in
    n) dry_run=true ;;
    f) force=true ;;
    h) print_usage_and_exit 0 ;;
    *) print_usage_and_exit 1 ;;
    esac
  done
  shift $((OPTIND - 1))

  if [[ ! -d "${work_dir}" ]]; then
    echo "Work directory doesn't exist: ${work_dir}"
    exit 1
  fi

  echo "Working in ${work_dir}..."

  declare -r cipd="${FUCHSIA_DIR}/.jiri_root/bin/cipd"
  declare -r dry_run=${dry_run}
  declare -r force=${force}
  declare jiri_entries="    <!-- termina guest images -->"

  # Ensure one of "dry-run" or "force" is given.
  if [ "$dry_run" == "$force" ];
  then
    print_usage_and_exit 1
  fi

  check_depot_tools

  board=`board_for_arch ${arch}`
  termina_revision=$TERMINA_REVISION

  jiri_entries="${jiri_entries}
  <package name=\"fuchsia_internal/linux/termina-${arch}\"
           version=\"<insert-instance-ID-here>\"
           path=\"prebuilt/virtualization/packages/termina_guest/images/${arch}\"
           internal=\"true\"/>"

  echo "*** Prepare chromeos tree"
  create_cros_tree "${work_dir}/cros" "${termina_revision}"

  echo "*** Prepare ANGLE and build"
  build_angle "${work_dir}/angle" "${arch}"

  # Tael board is 32bit userspace so we can't link in our 64bit libraries
  if [ "${arch}" == "x64" ]; then
    echo "*** Copy ANGLE outputs to chromeos tree"
    cp -f "${work_dir}/angle/out/libEGL.so.1" "${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files"
    cp -f "${work_dir}/angle/out/libGLESv2.so.2" "${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files"
  fi

  echo "*** Build Termina image"
  build_termina_image "${arch}" "${work_dir}/cros"

  echo "*** Copy Termina image"
  cp -av "${work_dir}/cros/chroot/home/${USER}/termina-${board}-image" "${work_dir}"

  options=()
  options+=("create")
  options+=("-in" "${work_dir}/termina-${board}-image")
  options+=("-name" "fuchsia_internal/linux/termina-${arch}")
  options+=("-install-mode" "copy")
  options+=("-tag" "termina-custom:${termina_revision}")

  echo cipd ${options[*]}
  if [ "$dry_run" != true ] ; then
    echo "*** Running CIPD - note the instance ID"
    ${cipd} ${options[*]}
    echo "Update //integration/fuchsia/prebuilts with the following:"
    echo "${jiri_entries}"
  fi

  echo "*** Done"
}

main "$@"
