
# The Device Protocol

Device drivers implement a set of hooks (methods) to support the
operations that may be done on the devices that they publish.

These are described below, including the action that is taken
by the default implementation that is used for each hook if the
driver does not provide its own implementation.

## version
This field must be set to `DEVICE_OPS_VERSION`
```
uint64_t version;
```

## open
The open hook is called when a device is opened via the device filesystem,
or when an existing open connection to a device is cloned (for example,
when a device fd is shared with another process).  The default open hook,
if a driver does not implement one, simply returns **ZX_OK**.

Drivers may want to implement open to disallow simultaneous access (by
failing if the device is already open), or to return a new **device instance**
instead.

The optional *dev_out* parameter allows a device to create and return a
**device instance** child device, which can be used to manage per-instance
state instead of all client connections interacting with the device itself.
A child created for return as an instance **must** be created with the
**DEVICE_ADD_INSTANCE** flag set in the arguments to **device_add()**.

```
zx_status_t (*open)(void* ctx, zx_device_t** dev_out, uint32_t flags);
```

## close
The close hook is called when a connection to a device is closed. These
calls will balance the calls to open.

**Note:** If open returns a **device instance**, the balancing close hook
that is called is the close hook on the **instance**, not the parent.

The default close implementation returns **ZX_OK**.
```
zx_status_t (*close)(void* ctx, uint32_t flags);
```

## unbind
The unbind hook is called when the parent of this device is being removed (due
to hot unplug, fatal error, etc).  At the point unbind is called, it is not
possible for further open calls to occur, but io operations, etc
may continue until those client connections are closed.

The driver should avoid further method calls to its parent device or any
protocols obtained from that device, and expect that any further such calls
will return with an error.

The driver should adjust its state to encourage its client connections to close
(cause IO to error out, etc), and call **device_remove()** on itself when ready.

The driver must continue to handle all device hooks until the **release** hook
is invoked.

```
void (*unbind)(void* ctx);
```

## release
The release hook is called after this device has been removed by **device_remove()**
and all open client connections have been closed, and all child devices have been
removed and released.

At the point release is invoked, the driver will not receive any further calls
and absolutely must not use the underlying **zx_device_t** or any protocols obtained
from that device once this method returns.

The driver must free all memory and release all resources related to this device
before returning.
```
void (*release)(void* ctx);
```

## read
The read hook is an attempt to do a non-blocking read operation.

On success *actual* must be set to the number of bytes read (which may be less
than the number requested in *count*), and return **ZX_OK**.

A successful read of 0 bytes is generally treated as an End Of File notification
by clients.

If no data is available now, **ZX_ERR_SHOULD_WAIT** must be returned and when
data becomes available `device_state_set(DEVICE_STATE_READABLE)` may be used to
signal waiting clients.

This hook **must not block**.

The default read implementation returns **ZX_ERR_NOT_SUPPORTED**.

```
zx_status_t (*read)(void* ctx, void* buf, size_t count,
                    zx_off_t off, size_t* actual);
```

## write
The write hook is an attempt to do a non-blocking write operation.

On success *actual* must be set to the number of bytes written (which may be
less than the number requested in *count*), and **ZX_OK** should be returned.

If it is not possible to write data at present **ZX_ERR_SHOULD_WAIT** must
be returned and when it is again possible to write,
`device_state_set(DEVICE_STATE_WRITABLE)` may be used to signal waiting clients.

This hook **must not block**.

The default write implementation returns **ZX_ERR_NOT_SUPPORTED**.

```
zx_status_t (*write)(void* ctx, const void* buf, size_t count,
                     zx_off_t off, size_t* actual);
```

## get_size
If the device is seekable, the get_size hook should return the size of the device.

This is the offset at which no more reads or writes are possible.

The default implementation returns 0.
```
zx_off_t (*get_size)(void* ctx);
```

## ioctl
The ioctl hook allows support for device-specific operations.

These, like read and write, must not block.

On success, **ZX_OK** must be returned and *out_actual* must be set
to the number of output bytes provided (0 if none).

The default ioctl implementation returns **ZX_ERR_NOT_SUPPORTED**.
```
zx_status_t (*ioctl)(void* ctx, uint32_t op,
                     const void* in_buf, size_t in_len,
                     void* out_buf, size_t out_len, size_t* out_actual);
```

## rxrpc
Only called for bus devices.
When the "shadow" of a busdev sends an rpc message, the
device that is shadowing is notified by the rxrpc op and
should attempt to read and respond to a single message on
the provided channel.

Any error return from this method will result in the channel
being closed and the remote "shadow" losing its connection.

This method is called with ZX_HANDLE_INVALID for the channel
when a new client connects -- at which point any state from
the previous client should be torn down.
```
zx_status_t (*rxrpc)(void* ctx, zx_handle_t channel);
```

## message
Process a FIDL rpc message.  This is used to handle class or
device specific messaging.  fuchsia.io.{Node,File,Device} are
handles by the devhost itself.

The entire message becomes the responsibility of the driver,
including the handles.

The txn provided to respond to the message is only valid for
the duration of the message() call.  It must not be cached
and used later.

If this method returns anything other than ZX_OK, the underlying
connection is closed.
```
zx_status_t (*message)(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn);
```

#### Device State Bits
```
#define DEV_STATE_READABLE ZX_USER_SIGNAL_0
#define DEV_STATE_WRITABLE ZX_USER_SIGNAL_2
#define DEV_STATE_ERROR    ZX_USER_SIGNAL_3
#define DEV_STATE_HANGUP   ZX_USER_SIGNAL_4
#define DEV_STATE_OOB      ZX_USER_SIGNAL_1
```

#### device_state_set
```
void device_state_set(zx_device_t* dev, zx_signals_t stateflag);
```
