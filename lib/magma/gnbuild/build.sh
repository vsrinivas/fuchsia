#!/bin/bash
set -e
fuchsia_root=`pwd`
build_dir=$fuchsia_root/out/Debug
echo "fuchsia_root=$fuchsia_root"
./buildtools/gn gen $build_dir --root=$fuchsia_root --dotfile=$fuchsia_root/magma/.gn
ninja -C $build_dir magma_service_driver
bootfs_path=$build_dir/bootfs
rm -rf $bootfs_path
mkdir -p $bootfs_path/bin
cp $build_dir/magma_service_driver $bootfs_path/bin/driver-pci-8086-1616
$fuchsia_root/buildtools/mkbootfs -v -o $build_dir/user.bootfs @$bootfs_path
magenta_build_dir=$fuchsia_root/magenta/build-magenta-pc-uefi
echo "Recommended bootserver command:"
echo ""
echo "$magenta_build_dir/tools/bootserver $magenta_build_dir/magenta.bin $build_dir/user.bootfs"
echo ""