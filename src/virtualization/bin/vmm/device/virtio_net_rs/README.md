# Virtio-net Device

The virtio-net device is in-process with the netstack preventing the need for a process hop
and additional packet copy. There is currently no support for a Rust netdevice server, so this
device uses Rust for the virtio bindings, and C++ via FFI for the netstack bindings.