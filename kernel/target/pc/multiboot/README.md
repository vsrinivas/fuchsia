# Multiboot trampoline loader

This directory implements a small "kernel" compatible with the
[Multiboot](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
specification that simulates a
[ZBI](../../../../system/public/zircon/boot/image.h)-compatible boot loader.
This makes it possible to boot a *complete x86-64 ZBI* via a
*Multiboot-compatible x86-32 boot loader* such as
[GRUB](https://www.gnu.org/software/grub/) or [QEMU](https://www.qemu.org/).

`multiboot.bin` is a Multiboot-compatible ELF kernel image.  It requires a
single *Multiboot module*, also known as the `initrd` or *RAM disk*, that
contains a complete ZBI image (kernel and BOOTFS).  With QEMU, use:
```
-kernel multiboot.bin -initrd zircon.zbi
```
