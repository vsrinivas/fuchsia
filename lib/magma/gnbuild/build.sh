#!/bin/bash
set -e
fuchsia_root=`pwd`
magenta_build_dir=$fuchsia_root/magenta/build-magenta-pc-uefi

bootfs_output_dir=$fuchsia_root/out/Debug
build_dir=$1
if [ "$build_dir" == "" ]
    then build_dir=$bootfs_output_dir
fi
bootfs_path=$build_dir/bootfs

bootfs_output_file=$bootfs_output_dir/user.bootfs
rm -f $bootfs_output_file

echo "fuchsia_root=$fuchsia_root build_dir=$build_dir"

./buildtools/gn gen $build_dir --root=$fuchsia_root --dotfile=$fuchsia_root/magma/.gn

ninja -C $build_dir magma_service_driver

rm -rf $bootfs_path
mkdir -p $bootfs_path/bin
cp $build_dir/msd-intel-gen $bootfs_path/bin/driver-pci-8086-1616

mkdir -p $bootfs_output_dir
./buildtools/mkbootfs -v -o $bootfs_output_file @$bootfs_path

echo "Recommended bootserver command:"
echo ""
echo "$magenta_build_dir/tools/bootserver $magenta_build_dir/magenta.bin $bootfs_output_file"
echo ""