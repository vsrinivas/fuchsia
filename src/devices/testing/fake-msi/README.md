This library provides fake replacements for the MSI syscalls for the purpose of
testing driver code in an unprivileged environment.  It works by defining strong
symbols for the following system calls:

- **zx_object_get_info**()
- **zx_msi_allocate**()
- **zx_msi_create**()
- **zx_interrupt_wait**()
