setenv bootcmd ''

setenv fk_kvers current
setenv fdtpath /dtbs/${fk_kvers}/${fdtfile}

setenv fdt_addr_r 0x10200000
setenv kernel_addr_r 0x10280000

setenv ramdisk_addr_r 0x18000000

setenv bootargs 'TERM=uart'

# We don't have a great way to calculate the ramdisk_end
# on the odroidc2 since the default u-boot build did not
# enable the setexpr command.  In order to avoid requiring
# users to build/update the u-boot from the prebuilt available
# from hardkernel we will just declare a really large ramdisk.
# the arm generic platform will get the base address from the
# fdt, but will then pull the size from the ramdisk itself, so
# this will work around bootloader limitations without breaking
# compatibility on other targets.

setenv ramdisk_end 0x70000000

load mmc 0:1 ${fdt_addr_r} ${fdtpath}
fdt addr ${fdt_addr_r}
fdt resize
load mmc 0:1 ${ramdisk_addr_r} odroidc2-bootdata.bin
fdt chosen ${ramdisk_addr_r} ${ramdisk_end}
load mmc 0:1 ${kernel_addr_r} odroidc2-zircon.bin

booti ${kernel_addr_r} - ${fdt_addr_r}
