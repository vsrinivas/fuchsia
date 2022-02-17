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
  echo "  update_cipd_prebuilts.sh work_dir (x64|arm64) [-t target] [-n] [-f]"
  echo ""
  echo "Where:"
  echo "      work_dir - An empty directory to place source trees and build artifacts."
  echo "      Note: script may generate contents of ~125 GB"
  echo ""
  echo "   -t target - An optional target: all,clean,debian,angle"
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

build_debian_drivers() {
  local -r debian_dir="$1"
  local -r arch="$2"

  if [ "${arch}" != "x64" ]; then
    echo "Debian build only supports x64"
  fi

  local chroot_dir="${debian_dir}/chroot"

  if [ ! -d ${debian_dir} ]; then
    mkdir -p ${debian_dir}/src

    sudo debootstrap --arch amd64 bullseye ${chroot_dir} http://deb.debian.org/debian

    # Bind mount sources into chroot so we can edit as regular user outside the chroot
    sudo mkdir ${chroot_dir}/src
    sudo mount --bind ${debian_dir}/src ${chroot_dir}/src

    sudo chroot ${chroot_dir} bash -c "dpkg --add-architecture i386"
    sudo chroot ${chroot_dir} bash -c "apt update"
    sudo chroot ${chroot_dir} bash -c "apt install --yes git gcc-multilib g++-multilib meson \
      python3-setuptools python3-mako rapidjson-dev googletest libvulkan-dev libvulkan-dev:i386 \
      pkg-config bison flex libwayland-dev libwayland-dev:i386 wayland-protocols \
      libwayland-egl-backend-dev libwayland-egl-backend-dev:i386 libdrm-dev libdrm-dev:i386 \
      libxrandr-dev libxrandr-dev:i386 libdrm-dev libdrm-dev:i386 libx11-xcb-dev \
      libx11-xcb-dev:i386 libxcb-dri2-0-dev libxcb-dri2-0-dev:i386 libxcb-dri3-dev \
      libxcb-dri3-dev:i386 libxcb-present-dev libxcb-present-dev:i386 libxcb-randr0 \
      libxcb-randr0:i386 libxcb-shm0 libxcb-shm0:i386 libxshmfence-dev libxshmfence-dev:i386 \
      zlib1g-dev zlib1g-dev:i386 libxfixes-dev libxfixes-dev:i386 libxcb-glx0-dev \
      libxcb-glx0-dev:i386 libxcb-shm0-dev libxcb-shm0-dev:i386 libxxf86vm-dev libxxf86vm-dev:i386 \
      libpciaccess-dev libpciaccess-dev:i386"

    # Mesa for 32bit ICD build
    git clone https://fuchsia.googlesource.com/third_party/mesa ${debian_dir}/src/mesa
    git clone https://fuchsia.googlesource.com/fuchsia ${debian_dir}/src/mesa/subprojects/fuchsia

    mkdir -p ${debian_dir}/src/mesa/build32
    echo "${meson_cross_file}" > ${debian_dir}/src/mesa/build32/crossfile

    (cd ${debian_dir}/src/mesa/subprojects/fuchsia && ln -s src/graphics/lib/magma/meson-top/meson.build meson.build)
    (cd ${debian_dir}/src/mesa/subprojects/fuchsia && ln -s src/graphics/lib/magma/meson-top/meson_options.txt meson_options.txt)

    sudo chroot ${chroot_dir} bash -c "PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig \
      meson --cross-file /src/mesa/build32/crossfile --buildtype release /src/mesa/build32 /src/mesa \
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
      -Dlibdir=lib/i386-linux-gnu"

    prefix="/src/zink/out/install"

    mkdir -p ${debian_dir}/src/zink/src ${debian_dir}${prefix}
    echo "${meson_cross_file}" > ${debian_dir}/src/zink/crossfile

    # For dependencies that are newer than the system provided, build from source
    git clone https://gitlab.freedesktop.org/wayland/wayland-protocols -b 1.24 ${debian_dir}/src/zink/src/wayland-protocols
    git clone https://gitlab.freedesktop.org/mesa/drm.git -b libdrm-2.4.109 ${debian_dir}/src/zink/src/drm
    git clone https://fuchsia.googlesource.com/third_party/mesa -b sandbox/zink-magma ${debian_dir}/src/zink/src/mesa

    sudo chroot ${chroot_dir} bash -c "meson --prefix=${prefix} /src/zink/src/wayland-protocols /src/zink/out/wayland-protocols"
    sudo chroot ${chroot_dir} bash -c "ninja -C /src/zink/out/wayland-protocols install"

    sudo chroot ${chroot_dir} bash -c "meson --prefix=${prefix} /src/zink/src/drm /src/zink/out/drm/build64"
    sudo chroot ${chroot_dir} bash -c "ninja -C /src/zink/out/drm/build64 install"
    sudo chroot ${chroot_dir} bash -c "PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig \
      meson --cross-file /src/zink/crossfile --prefix=${prefix} /src/zink/src/drm /src/zink/out/drm/build32"
    sudo chroot ${chroot_dir} bash -c "ninja -C /src/zink/out/drm/build32 install"

    sudo chroot ${chroot_dir} bash -c "PKG_CONFIG_PATH=${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig \
      meson --cross-file /src/zink/crossfile --buildtype release /src/zink/src/mesa /src/zink/out/mesa/build32 \
      -Ddrm-stubs=true \
      -Ddri-drivers= \
      -Dgallium-drivers=zink \
      -Dvulkan-drivers= \
      -Dgles1=enabled \
      -Dgles2=enabled \
      -Dopengl=true \
      -Dgbm=disabled \
      -Degl=enabled \
      -Dprefix=/usr \
      -Dlibdir=lib/i386-linux-gnu \
      -Ddri-search-path=/opt/google/cros-containers/drivers/lib32/dri \
      -Dsysconfdir=/opt/google/cros-containers/etc"

    sudo chroot ${chroot_dir} bash -c "PKG_CONFIG_PATH=${prefix}/lib64/pkgconfig:${prefix}/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig \
      meson --buildtype release /src/zink/src/mesa /src/zink/out/mesa/build64  \
      -Ddrm-stubs=true \
      -Ddri-drivers= \
      -Dgallium-drivers=zink \
      -Dvulkan-drivers= \
      -Dgles1=enabled \
      -Dgles2=enabled \
      -Dopengl=true \
      -Dgbm=disabled \
      -Degl=enabled \
      -Dprefix=/usr \
      -Dlibdir=lib/x86_64-linux-gnu \
      -Ddri-search-path=/opt/google/cros-containers/drivers/lib64/dri \
      -Dsysconfdir=/opt/google/cros-containers/etc"
  fi

  sudo chroot ${chroot_dir} bash -c "ninja -C /src/mesa/build32"
  sudo chroot ${chroot_dir} bash -c "ninja -C /src/zink/out/mesa/build64"
  sudo chroot ${chroot_dir} bash -c "ninja -C /src/zink/out/mesa/build32"
}

copy_debian_drivers() {
  local -r debian_dir="$1"
  local -r dest_dir="$2"

  mkdir -p "${dest_dir}/lib64"
  cp -fv "${debian_dir}/src/zink/out/mesa/build64/src/gallium/targets/dri/libgallium_dri.so" "${dest_dir}/lib64/zink_dri.so"
  cp -fv "${debian_dir}/src/zink/out/mesa/build64/src/glx/libGL.so.1.2.0" "${dest_dir}/lib64"
  cp -fv "${debian_dir}/src/zink/out/mesa/build64/src/egl/libEGL.so.1.0.0" "${dest_dir}/lib64"
  cp -fv "${debian_dir}/src/zink/out/mesa/build64/src/mapi/es2api/libGLESv2.so.2.0.0" "${dest_dir}/lib64"

  mkdir -p "${dest_dir}/lib32"
  cp -fv "${debian_dir}/src/mesa/build32/src/intel/vulkan/libvulkan_intel.so" "${dest_dir}/lib32"

  cp -fv "${debian_dir}/src/zink/out/mesa/build32/src/gallium/targets/dri/libgallium_dri.so" "${dest_dir}/lib32/zink_dri.so"
  cp -fv "${debian_dir}/src/zink/out/mesa/build32/src/glx/libGL.so.1.2.0" "${dest_dir}/lib32"
  cp -fv "${debian_dir}/src/zink/out/mesa/build32/src/egl/libEGL.so.1.0.0" "${dest_dir}/lib32"
  cp -fv "${debian_dir}/src/zink/out/mesa/build32/src/mapi/es2api/libGLESv2.so.2.0.0" "${dest_dir}/lib32"
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


build_extras_image() {
  local -r extras_dir="$1"
  local -r image_file="$2"

  declare -r BLOCK_SIZE=4096
  declare -r ADDITIONAL_BLOCKS=1024
  declare -r SIZE=$(du -sb ${extras_dir} | grep -o '^[0-9]*')
  declare -r BLOCKS=$(($((${SIZE}/${BLOCK_SIZE}))+${ADDITIONAL_BLOCKS}))

  rm -f "${image_file}"

  mke2fs -q -d "${extras_dir}" -t ext2 -b ${BLOCK_SIZE} "${image_file}" ${BLOCKS}
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

  target="all"

  while getopts "nft:h" FLAG; do
    case "${FLAG}" in
    n) dry_run=true ;;
    f) force=true ;;
    h) print_usage_and_exit 0 ;;
    t) target=${OPTARG} ;;
    *) print_usage_and_exit 1 ;;
    esac
  done
  shift $((OPTIND - 1))

  if [[ ! -d "${work_dir}" ]]; then
    echo "Work directory doesn't exist: ${work_dir}"
    exit 1
  fi

  if [[ "${target}" != "all" && "${target}" != "debian" && "${target}" != "angle" && "${target}" != "clean" ]]; then
    print_usage_and_exit 1
  fi

  echo "Working on target ${target} in ${work_dir}..."

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

  if [ "${target}" == "all" ]; then
    echo "*** Prepare chromeos tree"
    create_cros_tree "${work_dir}/cros" "${termina_revision}"
  fi

  if [[ "${target}" == "all" || "${target}" == "debian" ]]; then
    if [ "${arch}" == "x64" ]; then
      echo "*** Prepare debian and build"
      build_debian_drivers "${work_dir}/debian" "${arch}"

      echo "*** Copy debian drivers to chromeos tree"
      dest_dir=${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files/prebuilt-${arch}
      copy_debian_drivers "${work_dir}/debian" "${dest_dir}"
    fi
  fi

  if [[ "${target}" == "all" || "${target}" == "angle" ]]; then
    echo "*** Prepare ANGLE and build"
    build_angle "${work_dir}/angle" "${arch}"

    # Tael board is 32bit userspace so we can't link in our 64bit libraries
    if [ "${arch}" == "x64" ]; then
      echo "*** Copy ANGLE outputs to chromeos tree"
      dest_dir=${work_dir}/cros/src/third_party/chromiumos-overlay/media-libs/mesa/files/prebuilt-${arch}
      mkdir -p "${dest_dir}/angle"
      cp -fv "${work_dir}/angle/out/libEGL.so.1" "${dest_dir}/angle"
      cp -fv "${work_dir}/angle/out/libGLESv2.so.2" "${dest_dir}/angle"
    fi
  fi

  if [[ "${target}" == "all" ]]; then
    echo "*** Build Termina image"
    build_termina_image "${arch}" "${work_dir}/cros"

    echo "*** Copy Termina image"
    cp -av "${work_dir}/cros/chroot/home/${USER}/termina-${board}-image" "${work_dir}"

    echo "*** Build extras image"
    local -r extras_dir="${work_dir}/extras-${arch}"

    mkdir -p ${extras_dir}
    cp -av ${work_dir}/cros/chroot/build/${board}/usr/bin/aplay ${extras_dir}
    cp -av ${work_dir}/cros/chroot/build/${board}/usr/bin/arecord ${extras_dir}

    build_extras_image "${extras_dir}" "${work_dir}/termina-${board}-image/vm_extras.img"
  fi

  if [[ "${target}" == "all" ]]; then
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
  fi

  if [[ "${target}" == "clean" ]]; then
    if [[ -d "${work_dir}/debian/chroot/src" ]]; then
      sudo umount -q "${work_dir}/debian/chroot/src"
    fi
    sudo rm -rf ${work_dir}/*
  fi

  echo "*** Done"
}

main "$@"
