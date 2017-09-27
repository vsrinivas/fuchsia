#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script wraps imager.py and provides some configuration convenience
# functionality. For example if a directory containing the fuchsia build output
# is not supplied we assume it is two directories up from the build script and
# then in a sub-directory for a given architecture. We also set sensible
# defaults for things like partition size, etc.

set -e -u

# construct the path to our directory
host_type=$(uname -s)
script_dir=
if [ "$host_type" = "Darwin" ]; then
  script_dir=$(cd "$(dirname "$0")"; pwd)
elif [ "$host_type" = "Linux" ]; then
  script_dir=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
else
  echo "Unsupported OS, please use Mac or Linux"
  exit -1
fi

script_name=$(basename "$0")
expected_path="scripts/installer"
curr_dir=$(pwd)

# check that it looks like the script lives inside a fuchsia source tree
if [ $(dirname "$(dirname "$script_dir")")/"$expected_path" != "$script_dir" ]; then
  echo "It doesn't look like we're running in the right place, please make" \
    "sure the script is in a fuchsia source tree in scripts/installer"
  exit -1
fi

DEFAULT_SIZE_SYSTEM=4
DEFAULT_SIZE_EFI=1
BLOCK_SIZE=1024
STAGING_DIR="${script_dir}/../../out/build-installer"
# TODO take a size for the zircon partition as well
bytes_sys=$(($DEFAULT_SIZE_SYSTEM * 1024 * 1024 * 1024))
# FAT wants the sector count to be a multiple of 63 (for total sectors) and of
# 32 (sectors per track) and this value gets us close to 1GiB
bytes_efi=$(($DEFAULT_SIZE_EFI * 512 * 1040 * 32 * 63))
release=0
debug=0
platform="x86-64"
build_dir_fuchsia=""
minfs_path=""
build_dir_zircon=""
device_type="pc"
kernel_cmdline=""
bootdata=""
kernel_args=""
boot_manifest=""
enable_thread_exp=1
extras=("")

while (( "$#" )); do
  case $1 in
    "-u")
      shift
      bytes_sys=$(($1 * 1024 * 1024 * 1024))
      ;;
    "-h")
      echo "build-installable-usersfs.sh -u <SIZE> [-r|-d] [-p] [-b <BUILD DIR>]"
      echo "-u: size of system partition in GB"
      echo "-e: size of the EFI partition in GB"
      echo "-r: use the release build directory, should not be used with -d"
      echo "-d: use the debug build directory, should not be used with -r"
      echo "-p: platform architecture, eg. x86-64, arm, or arm-64"
      echo "-b: specify the build directory manually, this will cause -r and" \
        "-d arguments to be ignored"
      echo "-m: path to the host architecture minfs binary, perhaps you need" \
        "to run 'make' in zircon/system/uapp/minfs"
      echo "-a: artifacts directory for zircon, will be used to find files" \
        "to place on the EFI partition. If not supplied, this will be assumed" \
        "relative to fuchsia build directory."
      echo "-t: the device type, for example 'qemu', 'rpi', 'pc', etc"
      echo "-c: file containing kernel command line options"
      echo "-o: the kernel command line options to use. If the command line" \
        "contains spaces, the string should be quoted. If both this and -c are" \
        "supplied, these options will be appended to the command line file."
      echo "-x: bootdata location"
      echo "-w: location of the boot partition manifest"
      echo "-j: disable experimental thread prioritization"
      exit 0
      ;;
    "-j")
      enable_thread_exp=0
      ;;
    "-r")
      release=1
      ;;
    "-d")
      debug=1
      ;;
    "-p")
      shift
      platform=$1
      ;;
    "-b")
      shift
      build_dir_fuchsia=$1
      ;;
    "-m")
      shift
      minfs_path=$1
      ;;
    "-e")
      shift
      bytes_efi=$(($1 * 1024 * 1024 * 1024))
      ;;
    "-a")
      shift
      build_dir_zircon=$1
      ;;
    "-t")
      shift
      device_type=$1
      ;;
    "-c")
      shift
      kernel_cmdline=$1
      ;;
    "-o")
      shift
      kernel_args=$1
      ;;
    "-x")
      shift
      bootdata=$1
      shift
      ;;
    "-w")
      shift
      boot_manifest=$1
      ;;
    *)
      extras+=("$1")
      ;;
  esac
  shift
done

if [ "$build_dir_fuchsia" = "" ] || [ "$build_dir_zircon" = "" ]; then
  if [ "$release" -eq "$debug" ]; then
    if [ "$debug" -eq 0 ]; then
      debug=1
    else
      echo "Please choose release or debug, but not both"
      exit -1
    fi
  fi

  if [ "$release" -eq 1 ]; then
    build_variant="release"
  else
    build_variant="debug"
  fi
fi

arch=""
build_arch="x86_64"
case $platform in
  x86-64)
    arch="X64"
    build_arch="x86_64"
    ;;
  arm-64)
    arch="AA64"
    device_type="qemu"
    build_arch="aarch64"
    ;;
  \?)
    echo "Platform is not valid, should be x86-64, or arm-64!"
    exit -1
esac

echo "Building zircon for installer"
"${script_dir}/../build-zircon.sh" -t "$build_arch"

if [ "$build_arch" == "x86_64" ]; then
  build_arch="x86-64"
fi

# if the build directory is not specified, infer it from other parameters
if [ "$build_dir_fuchsia" = "" ]; then
  build_dir_fuchsia="${script_dir}/../../out/$build_variant-$build_arch"
else
  if [ "$release" -ne 0 ] || [ "$debug" -ne 0 ]; then
    echo "build directory is specified release arg ignored"
  fi
fi

if [ "$build_dir_zircon" = "" ]; then
  build_dir_zircon="${script_dir}/../../out/build-zircon/build-zircon-${device_type}-"
  if [ "$build_arch" = "aarch64" ]; then
    build_dir_zircon="${build_dir_zircon}arm64"
  elif [ "$build_arch" = "x86-64" ]; then
    build_dir_zircon="${build_dir_zircon}${build_arch}"
  fi
else
  if [ "$device_type" != "" ]; then
    echo "build directory is specified, type arg ignored"
  fi
fi

if [ "$minfs_path" = "" ]; then
  minfs_path=$build_dir_zircon/tools/minfs
fi

if [ ! -f "$minfs_path" ]; then
  echo "minfs path not found, please build minfs for your host and supply the" \
    "path"
  exit -1
fi

disk_path="${STAGING_DIR}/user_fs"
disk_path_efi="${STAGING_DIR}/efi_fs"

if [ ! -d "$build_dir_fuchsia" ]; then
  echo "Output directory '$build_dir_fuchsia' not found, please make sure you've"\
    "supplied the right build type and architecture OR correct path."
  exit -1
fi

if [ ! -d  "$STAGING_DIR" ]; then
  mkdir "$STAGING_DIR"
else
  rm -rf -- "$STAGING_DIR"/*
fi

set +e
mcpy_loc=$(which mcopy)
mmd_loc=$(which mmd)
lz4_path=$(which lz4)
mdir_loc=$(which mdir)

if [ "$mcpy_loc" = "" ] || [ "$mmd_loc" = "" ] || [ "$mdir_loc" == "" ]; then
  echo "Unable to find necessary mtools binaries, please install a copy of"\
    "mtools."
  exit -1
fi

if [ "$lz4_path" = "" ]; then
  echo "Unable to find lz4 tool, please install this package. On Ubuntu try"\
    "'apt-get install liblz4-tool'."
  exit -1
fi

emptyfile() {
  local path=$1
  local size=$2
  case $host_type in
    Darwin)
      mkfile -n "${size}" "$path"
      ;;
    Linux)
      truncate -s "${size}" "$path"
      ;;
    *)
      head -c "${size}" /dev/zero > "$path"
      ;;
  esac
}

set -e

echo "Building boot environment for installer"
gn_gen_path="packages/gn/gen.py"
ninja_path="buildtools/ninja"

"${script_dir}/../../$gn_gen_path" --outdir out/installer-system --target_cpu "$build_arch" --packages packages/gn/installer-system,packages/gn/install-fuchsia

sys_out="${script_dir}/../../out/installer-system-${build_arch}"
"${script_dir}/../../${ninja_path}" -C "$sys_out"

# create a suitably large file
echo "Creating system disk image, this may take some time..."
emptyfile "$disk_path" "$bytes_sys"
"$minfs_path" "$disk_path" mkfs

echo "Creating EFI disk image, this may take some time..."
emptyfile "$disk_path_efi" "$bytes_efi"

if [ "$host_type" = "Darwin" ]; then
  mount_path=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$disk_path_efi")
  # strip leading and trailing space
  mount_path="$(echo -e "${mount_path}" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
  newfs_msdos -F 32 "${mount_path}"
  hdiutil detach "$mount_path"
else
  mkfs.vfat -F 32 "$disk_path_efi"
fi

kernel_cmd_staging="${STAGING_DIR}/kernel_cmdline"
if [ "$kernel_cmdline" != "" ]; then
  cp "$kernel_cmdline" "$kernel_cmd_staging"
fi

if [ "$kernel_args" != "" ]; then
  if [ "$kernel_cmdline" = "" ]; then
    printf "$kernel_args" > "$kernel_cmd_staging"
  else
    printf "$kernel_args" >> "$kernel_cmd_staging"
  fi
fi

imager_cmd=( "${script_dir}"/imager.py --disk_path="$disk_path" --mcp_path="$mcpy_loc"
  --mmd_path="$mmd_loc" --lz4_path="$lz4_path" --build_dir="$build_dir_fuchsia"
  --temp_dir="$STAGING_DIR" --minfs_path="$minfs_path" --arch="$arch"
  --efi_disk="$disk_path_efi" --build_dir_zircon="$build_dir_zircon"
  --bootdata="$bootdata" --boot_manifest="$boot_manifest"
  --mdir_path="$mdir_loc" --runtime_dir="$sys_out" "${extras[@]}" )

if [ "$enable_thread_exp" -eq 0 ]; then
  imager_cmd+=("--disable_thread_exp")
fi

if [ "$kernel_cmdline" != "" ] || [ "$kernel_args" != "" ]; then
  imager_cmd+=("--kernel_cmdline=${kernel_cmd_staging}")
fi

${imager_cmd[@]}

echo "Built disks: $disk_path_efi & $disk_path"
