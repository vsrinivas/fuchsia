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

# Cross compile filed used for building 32bit Vulkan ICD
meson_cross_file="
   [binaries]
   c = '/usr/bin/gcc'
   cpp = '/usr/bin/g++'
   ar = '/usr/bin/gcc-ar'
   strip = '/usr/bin/strip'
   pkgconfig = '/usr/bin/pkg-config'
   llvm-config = '/usr/bin/llvm-config32'
   [properties]
   c_args = ['-m32']
   c_link_args = ['-m32']
   cpp_args = ['-m32']
   cpp_link_args = ['-m32']
   [host_machine]
   system = 'linux'
   cpu_family = 'x86'
   cpu = 'i686'
   endian = 'little'"

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

  # Apply patches to termina temporarily
  (cd src/platform/tremplin; git fetch https://chromium.googlesource.com/chromiumos/platform/tremplin refs/changes/34/3302234/1 && git cherry-pick FETCH_HEAD)

  # Switch to Fuchsia's mesa branch
  (cd src/third_party/mesa && \
    git config remote.fuchsia.url >&- || \
    git remote add fuchsia https://fuchsia.googlesource.com/third_party/mesa && \
    git remote update fuchsia && \
    git checkout fuchsia/main)

  if [ ! -d src/third_party/fuchsia ]; then
    git clone https://fuchsia.googlesource.com/fuchsia src/third_party/fuchsia
  fi

  popd
}

build_debian_32bit_icd() {
  local -r debian_dir="$1"
  local -r arch="$2"

  if [ "${arch}" != "x64" ]; then
    echo "Debian build only supports x64"
  fi

  if [ ! -d ${debian_dir} ]; then
    sudo debootstrap --arch amd64 bullseye ${debian_dir} http://deb.debian.org/debian
    sudo chroot ${debian_dir} bash -c 'dpkg --add-architecture i386'
    sudo chroot ${debian_dir} bash -c 'apt update'
    sudo chroot ${debian_dir} bash -c 'apt install --yes git gcc-multilib g++-multilib meson \
      python3-setuptools python3-mako rapidjson-dev googletest libvulkan-dev:i386 pkg-config:i386 \
      bison:i386 flex:i386 libwayland-dev:i386 wayland-protocols libxrandr-dev:i386 libdrm-dev:i386\
      libx11-xcb-dev:i386 libxcb-dri2-0-dev:i386 libxcb-dri3-dev:i386 libxcb-present-dev:i386 \
      libxcb-randr0 libxcb-randr0:i386 libxcb-shm0 libxcb-shm0:i386 libxshmfence-dev:i386 \
      zlib1g-dev:i386'

    sudo chroot ${debian_dir} bash -c 'git clone https://fuchsia.googlesource.com/third_party/mesa'
    sudo chroot ${debian_dir} bash -c 'git clone https://fuchsia.googlesource.com/fuchsia mesa/subprojects/fuchsia'

    sudo chroot ${debian_dir} bash -c 'mkdir -p mesa/build'
    sudo chroot ${debian_dir} bash -c "echo \"${meson_cross_file}\" > mesa/build/crossfile"

    sudo chroot ${debian_dir} bash -c 'cd mesa/subprojects/fuchsia && ln -s src/graphics/lib/magma/meson-top/meson.build meson.build'
    sudo chroot ${debian_dir} bash -c 'cd mesa/subprojects/fuchsia && ln -s src/graphics/lib/magma/meson-top/meson_options.txt meson_options.txt'

    sudo chroot ${debian_dir} bash -c "meson --cross-file mesa/build/crossfile mesa/build mesa \
      -Ddriver-backend=magma \
      -Ddri-drivers= \
      -Dgallium-drivers= \
      -Dvulkan-drivers=intel \
      -Dgles1=disabled \
      -Dgles2=disabled \
      -Dopengl=false \
      -Dgbm=disabled \
      -Degl=disabled \
      -Dprefix=/usr \
      -Dlibdir=lib/i386-linux-gnu \
      -Dfuchsia:with_tests=true"
  fi

  sudo chroot ${debian_dir} bash -c 'ninja -C mesa/build'
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
    ozone_platform_x11=true \
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

  # Switch to source build of tremplin
  cros_sdk bash -c "cros_workon --board=${board} start tremplin"

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

  if [ "${arch}" == "x64" ]; then
    echo "*** Prepare debian and build"
    build_debian_32bit_icd "${work_dir}/debian" "${arch}"

    echo "*** Copy 32bit ICD to chromeos tree"
    mkdir -p "${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files/lib32"
    cp -f "${work_dir}/debian/mesa/build/src/intel/vulkan/libvulkan_intel.so" "${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files/lib32/libvulkan_intel.so"
  fi

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
