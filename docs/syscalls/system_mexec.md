# mx_system_mexec

## NAME

mx_system_mexec - Soft reboot the system with a new kernel and bootimage

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_system_mexec(mx_handle_t kernel_vmo,
                            mx_handle_t bootimage_vmo);
```

## DESCRIPTION

**mx_system_mexec**() accepts two vmo handles: *kernel_vmo* should contain a
kernel image and *bootimage_vmo* should contain an initrd whose address shall
be passed to the new kernel as a kernel argument.

Upon success, *mx_system_mexec* shall supplant the currently running kernel
image with the kernel image contained within *kernel_vmo*, load the ramdisk
contained within *bootimage_vmo* to a location in physical memory and branch
directly into the new kernel while providing the address of the loaded initrd
to the new kernel.

## RETURN VALUE

**mx_system_mexec**() shall not return upon success.

## BUGS

This syscall should be very privileged.
