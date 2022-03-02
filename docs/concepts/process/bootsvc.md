# Bootsvc [Deprecated]

Bootsvc is now a stub which only exists to load component manager. It is possible to
boot directly into component manager from userboot by passing the following
[kernel command line argument](/docs/reference/kernel/kernel_cmdline.md):

```
userboot.next=bin/component_manager+fuchsia-boot:///#meta/root.cm+--config+/boot/config/component_manager+--host_bootfs
```

---

`bootsvc` is (typically) the first program loaded by usermode (contrast with
[userboot](userboot.md), which is loaded by the kernel).  `bootsvc` provides
several system services:

- A filesystem service with the contents of the bootfs (/boot)
- A loader service that loads from that bootfs

After preparing these services, it launches one program from the bootfs.  The
program may be selected with a [kernel command line argument](/docs/reference/kernel/kernel_cmdline.md)
`bootsvc.next` (this default to `bin/component_manager` currently).  The
launched program is provided with the bootfs mounted at `/boot` and the loader
service. `bootsvc.on_next_process_exit` controls whether bootsvc reboots or
shuts down the device when the process it starts exits.  The kernel command
line arguments are provided to it via `envp`.  See the documentation in
[//src/bringup/bin/bootsvc](/src/bringup/bin/bootsvc/) for more details.
