# The Gigaboot boot loader

The Gigaboot boot loader is a UEFI boot shim for Zircon that can load images via chaining from iPXE,
from a UEFI-accessible filesystem, or from local disk partitions.

## Usage

Arguments are taken from the kernel command line, or additionally from the EFI command line for
Gigaboot itself.

* `bootloader.zircon-a`: Sets the fallback filename to use for loading the Zircon-A image (default:
  `zircon.bin`).
* `bootloader.zircon-b`: Sets the fallback filename to use for loading the Zircon-B image (default:
  `zedboot.bin`).
* `bootloader.zircon-r`: Sets the fallback filename to use for loading the Zircon-R (recovery) image
  (default: none).
* `bootloader.fbres`: Sets the framebuffer resolution (e.g. `1024x768`; default: automatic).
* `bootloader.default`: Sets the default boot choice in the boot menu; default is the first in the
  list.  Possible values are `network`, `local` or `zedboot`.

## Fastboot support

Gigaboot supports fastboot over UDP. You can enter fastboot mode by pressing `f`
during boot, or by invoking `dm reboot-bootloader` from Fuchsia.

### Protocol Information

Gigaboot implements the fastboot over UDP protocol described
[here](https://android.googlesource.com/platform/system/core/+/HEAD/fastboot/README.md).

### Supported Commands

| Command | Description |
| ------- | ----------- |
| `reboot` |  Reboots the device into the current active partition, as determined by ABR. |
| `reboot-recovery` | Reboots the device into R. |
| `reboot-bootloader` | Reboots the device into Fastboot mode. |
| `flash [partition] [image]` | Flashes the given image onto the given partition. |
| `erase [partition]` | Erases a partition by writing 0xff to it. |
| `getvar [varname]`  | Gets the value of the given variable. |
| `set_active [a|b]` | Marks one of the (a\|b) slots active. |

The partitions available for flashing/erasing are listed
[here](https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/public/zircon/hw/gpt.h).

### Supported Variables

| Variable | Description |
| -------- | ----------- |
| `all` | A meta-variable that lists all variables stored in the bootloader. |
| `max-download-size` | The maximum image size that can be sent over |
| `slot-count` | The number of slots that can be set active on the device. |
| `bootloader-min-versions` | The minimum required version of the bootloader. |
| `current-slot` | The slot currently marked active by ABR. |
| `slot-retry-count:[a|b]` | The number of boot attempts on the given slot. |
| `slot-successful:[a|b]` | True if the given slot has booted successfully. |
| `slot-unbootable:[a|b]` | True if the given slot is not bootable. |
| `version` | The fastboot protocol version. |

## Chaining with iPXE

Here is an example iPXE script showing how to chain Gigaboot.  In this example, the files are loaded
from a web server running on 192.168.42.128; we chain from Gigaboot into Zedboot.

```
#!ipxe

echo Chain loader

:loop2
prompt --key 0x02 --timeout 3000 Chain Loader: Press Ctrl-B for the iPXE command line... && shell ||
kernel http://192.168.42.128/gigaboot.efi bootloader.default=local bootloader.zircon-a=zedboot.zbi || goto loop2

:zedboot
initrd http://192.168.42.128/zedboot.zbi || goto loop2

:boot
boot || goto loop2
```

## Build notes

Since UEFI images are in PE32+ file format, we require that our binaries be position independent
executables with no relocations. For the most part this does not require any extra effort on x86-64,
but it does mean that you cannot statically initialize any variables that hold an address. (These
addresses may be assigned at runtime however.)


## External Dependencies

qemu-system-x86_64 is needed to test in emulation; gnu parted and mtools are needed to generate the
disk.img for Qemu.


## Useful Resources & Documentation

ACPI & UEFI Specifications: http://www.uefi.org/specifications

Intel 64 and IA-32 Architecture Manuals:
http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html

Tianocore UEFI Open Source Community (Source for OVMF, EDK II Dev Environment, etc):
http://www.tianocore.org/ https://github.com/tianocore

iPXE: http://ipxe.org

