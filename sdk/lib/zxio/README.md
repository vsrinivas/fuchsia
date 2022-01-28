# zxio

The zxio client library provides an object oriented abstraction on top of basic
I/O primitives such as files, pipes, directories, and sockets. This library is
intended for use in low-level system libraries such as fdio and language
runtimes to provide a minimal abstraction over different representations and
protocols used for common primitives.

Internally, this library uses the FIDL fuchsia.io* family of protocols and
Zircon syscalls.

# Objects

The zxio library is based on objects of type zxio_t. An object represents a
single logical entity and a set of operations that can be performed on that
entity. Internally an object may contain one or more Zircon handles to kernel
objects.

## Storage and lifetime of objects

The caller of the zxio library is responsible for providing storage for zxio
objects and is responsible for their lifetime. A zxio object lives within an
instance of the zxio_storage_t type.

## Kernel objects

A zxio object may allocate and retain ownership of one or more kernel objects
during the course of operation. Destroying an object will close all handles
owned by a zxio object. Calling `zxio_release()` will close all handles except for
the extracted primary object handle.

Functions with `zx_handle_t` parameters take ownership of the provided handles in
all cases including errors unless the function documentation specifically states
otherwise.

Functions with `zx_handle_t*` out parameters will do one of the following unless
the function documentation specifically states otherwise:

- Store a valid handle value into the provided address and return success. The
caller has ownership of the handle in this case.
- Store the value `ZX_HANDLE_INVALID` into the provided address and return an
error code.
- Do not store anything into the provided address and return an error code.

## Object types

The zxio library has an extensible set of object types. The library provides
many object types internally and allows users of the library to define their own
types by implementing the zxio ops table defined in zxio/ops.h. Custom object
implementers should adhere to the threading model for those objects.

## Threading model

Operations on zxio objects are thread-safe unless otherwise noted below. The
zxio library will internally synchronize any local state changes.

### Creation and destruction

A zxio object must be fully created with one of the `zxio_create..()` functions
before it can be used.

A zxio object is destroyed with the `zxio_close()` call. The caller is responsible
for ensuring that the zxio object is not in use on any thread before calling
`zxio_close()`. The `zxio_close()` operations always destroys the object and frees
any resources associated with the object even in error cases. The close
operation cannot be retried.

The storage underlying the zxio object should not be modified except by the zxio
library or custom object implementations and must remain valid until after
`zxio_close()` returns.

### Intrusive and compound operations

Some zxio operations provide access to or rely on the internal state of the
object and have additional considerations.

- `zxio_release()` extracts a handle from the zxio object and is partially
destructive on the object. The caller is responsible for ensuring that the zxio
object is not in use on any thread before calling `zxio_release()`. After
calling `zxio_release()`, the only operation that is safe to perform on the zxio
object is `zxio_close()`. `zxio_close()` must be called before the storage
underlying the object is deallocated.

- `zxio_borrow()` returns a handle to the zxio object's primary object if it
exists. The caller must take care to ensure that they do not close this handle
and that any operations they perform are safe with concurrent operations on the
object.

- `zxio_dirent_iterator_init()` creates an iterator object tied to a directory
object. The directory object must outlive the iterator object.

- `zxio_watch_directory()` initiates a watch operation tied to a directory object.
The operation must conclude before the directory can be destroyed.

## Blocking and cancellation

Many zxio operations are synchronized with a kernel object and possibly remote
server before returning. These operations will return the error
ZX_ERR_WOULD_BLOCK if the operation is unable to make progress in the object's
current state. Such operations may block if the remote server is unresponsive.
They should not block based on the object's internal state.

Operations which will never synchronize with a remote server are
documented as such. Some operations also expose a variant with the suffix
`_async` to indicate that they do not block and that the caller is responsible
for completing the remainder of the operation.  `zxio_open_async()` is an
example of such an operation. It solicits an event from the server backing the
opened object into a provided channel object. The caller can wait for the
channel to become readable using an asynchronous waiting facility such as a
`zx_port_wait()` and then call `zxio_open_with_on_open()` to process the event
when it is ready, or simply call `zxio_open_with_on_open()` to block until an
event is ready.

The zxio library does not currently provide any mechanism for canceling pending
or concurrent blocking operations on an object.

## Asynchronously waiting for state changes

The zxio library supports asynchronously waiting for objects to change state.
This can be used to defer operations until they are likely to make progress. To
use this facility:

1. Call `zxio_wait_begin()` with a set of states of interest. This will produce
a `zx_handle_t` value and a `zx_signals_t` value. Note that this operation does
not transfer ownership of the handle.
2. Register a wait on the handle + signals tuple with `zx_object_async_wait()`
or another Zircon waiting operation.
3. When the wait completes, call `zxio_wait_end()` with the values from kernel.
This will produce a `zxio_signals_t` value reflecting the object's new state.
4. Make calls based on the new state.

As the handle value produced by `zxio_wait_begin()` is borrowed from the zxio
object, it is unsafe to destroy the zxio object or release and close the
object's primary handle and then register and async wait.

This can be used to implement blocking calls by attempting the operation and
then blocking with `zx_object_wait_one()` if the operation returns
`ZX_ERR_WOULD_BLOCK`.

# Allocations, buffers, and strings

## Heap allocations

The zxio library currently allocates internal heap buffers for some operations.
These allocations are internal to the library and not exposed to callers. Custom
object implementations should avoid heap allocations where possible and maintain
ownership internal to the object when not.

TODO(https://fxbug.com/91172): Remove all of the heap allocations from inside
zxio and update this text.

## Buffers

Many zxio operations read or write to caller allocated buffers. The caller must
take care to ensure that these buffers are of the required size and alignment
and that they remain valid for the duration of the call.

## Stack usage

The zxio library attempts to use a limited amount of stack space to be usable in
as many situations as possible. Zxio does not perform any dynamically sized
stack allocations. There is currently no concrete upper bound on the stack usage
of the zxio library.


## Strings

The zxio library represents path components as strings with a pointer to a
buffer and an length. Path components cannot contain embedded nulls. The zxio library
does not require or set a null terminator.

When interfacing with C-style strings, compute a string length explicitly for
input parameters and allocate space for a null terminator for output parameters.

# Linkage

The zxio library does not maintain internal static state and can be used as a
static library. It internally uses the C++ standard library.

## zxio_standalone

libzxio_standalone.so is provided as a standalone shared library version of the
zxio with most dependencies statically linked in (including the C++ standard
library). This library depends dynamically on a subset of the C standard
library, many vVDSO calls, and the __zx_panic symbol. The exact list of symbol
dependencies is listed in zxio_standalone.imported_symbols.allowlist.
