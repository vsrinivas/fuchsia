This library provides the scaffolding for creating fake userspace versions
of kernel objects for testing. A fake handle table is maintained by the library,
and a fake Object base class is provided to build derived object types. It is
mostly useful if your goal is to test interfaces that *must* go through syscalls
or pretend to interact with hardware that cannot be mocked or faked easily. Bus
drivers and platform level authentication & token mechanisms like Resources, BTIs,
and the like are the target audience. At this time the library only supports single
process tests, and each statically linked binary will have its own fake handle table.

The library provides strong symbols for the following syscalls so that they can
be dispatched to virtual methods within the implemented Object:

- **zx_object_get_child**()
- **zx_object_get_info**()
- **zx_object_get_property**()
- **zx_object_set_profile**()
- **zx_object_set_property**()
- **zx_object_signal**()
- **zx_object_signal_peer**()
- **zx_object_wait_one**()
- **zx_object_wait_async**()

In addition, the following handle-related syscalls are provided to manipulate
fake handles vended by the library:

- **zx_handle_duplicate**()
- **zx_handle_replace**()
- **zx_handle_close**()
- **zx_handle_close_many**()

The library provides **zx_object_wait_many**() but its purpose is to catch errant
calls to it that contain a fake handle in the zx_wait_item_t list. Waiting on multiple
handles with one or more fake handles in the mix is not supported.