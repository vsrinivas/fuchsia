This library provides a fake replacement Resource syscall purpose of testing
driver code in an unprivileged environment.  It works by defining strong symbols
for the following system calls:

- **zx_object_get_info**()
- **zx_resource_create**()
- **zx_vmo_create_physical**()
- **zx_ioports_request**()
- **zx_ioports_release**()

The library exposes methods for creating and destroying fake BTI "handles" that
are compatible with the fake BTI syscalls.  It is not safe to use any other
handle operations on the fake BTI and PMT handles.
