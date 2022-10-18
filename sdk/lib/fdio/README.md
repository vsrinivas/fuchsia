# Fuchsia fdio Library

The core Fuchsia method for talking about files and other I/O primitives are
FIDL protocols over Zircon channels. But since most applications are written
against Unix integer file descriptors ("FD"s), Fuchsia provides the FDIO library
to adapt the core protocols to POSIX-like file descriptors.

To see how FDIO fits into the overall I/O stack, see [life of an
open](/docs/concepts/filesystems/life_of_an_open.md).

## Conceptual overview

### Converting between FDs and kernel handles

The main functions to convert between channels and file descriptors are:

  * [fdio_fd_create()] creates a file descriptor from a channel.
  * [fdio_get_service_handle()] converts from a file descriptor to a kernel
    handle. This is normally used to connect to FIDL services in the `/svc`
    directory.

These other variants also create handles from file descriptors. A "transfer"
means that the source file descriptor is closed and ownership transferred to the
`out_handle` (this is identical to [fdio_get_service_handle()]. However, if a
file descriptor has been `dup()`ed and there are more than one descriptor
representing the kernel handle, it can not be simply closed. In these cases, the
file descriptor can be "cloned" meaning that the original file descriptor is not
modified. The output handle is a new handle that the caller owns.

  * [fdio_fd_clone()] creates a handle for the given FD.
  * [fdio_fd_transfer()] closes the FD (if possible) and returns the handle.
  * [fdio_fd_transfer_or_clone()] transfers with a clone fallback.
  * [fdio_cwd_clone()] creates a handle to the current working directory.

Internally, FDIO uses the opaque `fdio_t` type to refer to a file descriptor.
These can be separately created and bound to channels and FDs:

  * [fdio_create()]
  * [fdio_default_create()]
  * [fdio_null_create()]
  * [fdio_fd_create_null()]
  * [fdio_bind_to_fd()]
  * [fdio_unbind_from_fd()]

### zxio integration

The fdio library is built on top of the zxio library. These functions provide
integration between fdio and zxio.

  * [fdio_get_zxio()]
  * [fdio_zxio_create()]

### Waiting

  * [fdio_wait_fd()] waits for events on a file descriptor.
  * [fdio_handle_fd()] creates a file descriptor from a kernel handle
    specifically for waiting (via `select()`, `epoll()`, etc.).

### Watching

Clients can use watch for changes in a given directory. This does not watch
for changes in subdirectories of the given directory.

  * [fdio_watch_directory()]

### VMO functions

Most standard Fuchsia files are represented by an underlying [virtual memory
object](/docs/reference/kernel_objects/vm_object.md) (VMO). FDIO provides access
to the kernel handle for this VMO when low-level operations are required.

These functions (which all have the same signature) convert a FDIO file handle
to a kernel handle to a VMO. They differ by the handling of error conditions and
the type of VMO provided. Since not all FDIO file handles represent normal files
and filesystems are not obligated to provide a VMO representation of a file,
callers should consider what behavior they want in such cases.

  * [fdio_get_vmo_exact()]
  * [fdio_get_vmo_clone()]
  * [fdio_get_vmo_copy()]
  * [fdio_get_vmo_exec()]

### Directories

These functions provide ways to open files or connect to services in
directories. To open or connect, a client will create a channel and send the
handle to the server end of that channel (the `request` parameters)
to the service for the directory (identified here either by handle file
descriptor, or by name bound in the current process' installed namespace).

There are also variants that automatically create the channel and register the
client end as a file descriptor in the current process.

  * [fdio_service_connect()]
  * [fdio_service_connect_at()]
  * [fdio_service_connect_by_name()]
  * [fdio_open()]
  * [fdio_open_at()]
  * [fdio_open_fd()]
  * [fdio_open_fd_at()]
  * [fdio_service_clone()]
  * [fdio_service_clone_to()]

### Namespaces

A namespace represents an application's view of a filesystem. The application's
view into the rest of the system is constructed by binding services (such as a
directory in a filesystem) to names in the namespace (such as "/data").
Typically applications only interact with their installed but other namespaces
can be constructed and destroyed if desired.

  * [fdio_ns_get_installed()] returns the application-global "installed" namespace.
  * [fdio_ns_create()]
  * [fdio_ns_destroy()]

An application can bind or unbind names in a namespace:

  * [fdio_ns_bind_fd()]
  * [fdio_ns_bind()]
  * [fdio_ns_unbind()]
  * [fdio_ns_is_bound()]

Namespaces support basic filesystem-like operations. These functions are used as
the basis for higher-level functions in FDIO like the directory functions:

  * [fdio_ns_opendir()]
  * [fdio_ns_chdir()]
  * [fdio_ns_open()]
  * [fdio_ns_service_connect()]

Namespaces can be converted into parallel arrays of path/handle/type. This
flat namespace structure is typically used to communicate a namespace between
processes:

  * [fdio_ns_export()]
  * [fdio_ns_export_root()]
  * [fdio_ns_free_flat_ns()]

### Process spawning

The fdio library provides process spawning functions that allow some or all of
the current process' environment to be shared. The spawned executable can be
identified either by a `path` in the current process' installed namespace or by
a VMO containing the binary.

  * [fdio_spawn()]
  * [fdio_spawn_etc()]
  * [fdio_spawn_vmo()]

### Other functions

These interfaces exist to allow integration of FDIO file descriptors with
handle-centric message loops. If used incorrectly they can corrupt the internal
FDIO state.

  * [fdio_unsafe_fd_to_io()]
  * [fdio_unsafe_borrow_channel()]
  * [fdio_unsafe_release()]
  * [fdio_unsafe_wait_begin()]
  * [fdio_unsafe_wait_end()]

This function assists in creating POSIX-style pipes that will be shared with
other processes:

  * [fdio_pipe_half()]
