# make-efi

A host tool for constructing EFI ESP partitions.

## Usage

``` bash
make-efi -target efi.blk -size $((63*1024*1024)) -offset 0 -kernel zircon.bin -ramdisk ramdisk.bin -efi-bootloader bootx64.efi -mkfs mkfs-msdosfs
```

Notes:

* mkfs is required, and must be a path to the zircon mkfs-msdosfs host tool.
* Size and offset are optional. They are most useful when writing into an existing disk image or block device, such as when writing into a GPT.
* The -ramdisk and -efi-bootloader are optional.
* A -manifest option supports `dst=src\n` formats and all files will be added. This is most useful for targets that require extra files, such as arm targets with extra firmware.
