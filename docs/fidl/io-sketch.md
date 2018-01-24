# FIDL 2.0: I/O Sketch

Status: DRAFT

Author: jeffbrown@google.com

This document shows a sketch of how the Fuchsia Interface Definition Language
(FIDL) v2.0 might be used to describe and implement an I/O protocol.

See [FIDL 2.0: Overview](index.md) for more information about FIDL's overall
purpose, goals, and requirements, as well as links to related documents.

**WORK IN PROGRESS**

[TOC]

## I/O Protocol Declaration

This example is a hypothetical extensible I/O protocol with device class
specific sub-interfaces. Something similar might be used to define part of the
Zircon I/O subsystem.

The example uses a C-like style to conform to Zircon conventions for the
purpose of this exposition. (Actual style conventions TBD. Maybe it'll be Google
C++. Maybe not.)

We declare several interfaces to represent an interface hierarchy. The goal here
is to model the set of I/O operations in an extensible loosely object-oriented
manner without relying on **ioctl** style trampolines.

### FIDL Files

#### file.fidl

```
library fdio;

const uint32 FILE_CLASS = 0x1000;

// Represents a file-like object whose contents can be read or written.
interface file {
    // Gets the class of interface supported by this file.
    // Class constants are declared alongside their interfaces.
    0x1001: get_class() -> (uint32 class);

// Obtain backing VMO, or a null handle if not available.
0x1002: get_vmo() -> (uint32 status, handle<vmo>? vmo);

    // Generic read operation.
0x1003: read(uint32 size) -> (uint32 status, vector<uint8> buffer);

// Generic write operation.
0x1004: write(vector<uint8> buffer) -> (uint32 status);
};
```

#### device.fidl

```
library fdio;

const uint32 DEVICE_CLASS = 0x2000;

interface device : file {
    0x2001: bind(string:400 device) -> (uint32 status);
    0x2002: watch_dir() -> (uint32 status, watch_listener l);
    0x2003: get_event_handle() -> (uint32 status, handle<event> event);
    0x2004: get_driver_name() -> (uint32 status, string:32 name);
    0x2005: get_device_name() -> (uint32 status, string:32 name);
    0x2006: debug_suspend() -> (uint32 status);
    0x2007: debug_resume() -> (uint32 status);
    0x2008: sync() -> (uint32 status);
};

interface watch_listener {
    0x0001: notify();
};
```

#### ethernet.fidl

```
library fdio;

const uint32 ETHERNET_CLASS = 0x3000;

interface ethernet : device {
    0x3001: get_info() -> (uint32 status, ethernet_info info);
    0x3002: get_fifos() -> (uint32 status,
    handle<fifo> tx, handle<fifo> rx,
    uint32 tx_depth, uint32 rx_depth);
    0x3003: set_io_buffer(handle<vmo> vmo) -> (uint32 status);
    0x3004: start() -> (uint32 status);
    0x3005: stop() -> (uint32 status);
    0x3006: tx_listen_start() -> (uint32 status);
    0x3007: tx_listen_stop() -> (uint32 status);
};

struct ethernet_info {
    uint32 feature;
    uint32 mtu;
    uint8[6] mac;
};
```

### Generated C Code

#### file.fidl-c.h

```
##include <fidl-c.h>

##define FILE_CLASS 0x1000

enum {
    fdio_FILE_ORDINAL_GET_CLASS = 0x1001,
    fdio_FILE_ORDINAL_GET_VMO = 0x1002,
    fdio_FILE_ORDINAL_READ = 0x1003,
    fdio_FILE_ORDINAL_WRITE = 0x1004,
};

typedef struct fdio_file_get_class_result {
    uint32_t status;
} fdio_file_get_class_result_t;

typedef struct fdio_file_get_vmo_result {
    uint32_t status;
    zx_handle_t vmo;
} fdio_file_get_vmo_result_t;

typedef struct fdio_file_read_args {
    uint32_t size;
} fdio_file_read_args_t;

typedef struct fdio_file_read_result {
    uint32_t status;
    fidl_vector_t buffer;
} fdio_file_read_result_t;

typedef struct fdio_file_write_args {
    fidl_vector_t buffer;
} fdio_file_write_args_t;

typedef struct fdio_file_write_result {
    uint32_t status;
} fdio_file_write_result_t;

/* Encoding and introspection tables go here... */
```

#### device.fidl-c.h

```
/* Similar to file.fidl-c.h */
```

#### ethernet.fidl-c.h

```
/* Similar to device.fidl-c.h but we have a new struct */

typedef struct ethernet_info {
    uint32_t feature;
    uint32_t mtu;
    uint8_t[6] mac;
    uint8_t[2]; // padding
} ethernet_info_t;
```

### Generated Native Style C++ Code

#### file.fidl-cpp-native.h

```
##include <fidl-cpp-native.h>
##include "file.fidl-c.h"

namespace fdio {

constexpr uint32_t FILE_CLASS = 0x1000;

namespace file_native {

enum class ordinal {
    GET_CLASS = fdio_FILE_ORDINAL_GET_CLASS,
    GET_VMO = fdio_FILE_ORDINAL_GET_VMO,
    READ = fdio_FILE_ORDINAL_READ,
    WRITE = fdio_FILE_ORDINAL_WRITE,
};

struct get_class_result {
    uint32_t status;
};

struct get_vmo_result {
    uint32_t status;
    zx::unowned_vmo vmo;
};

struct read_args {
    uint32_t size;
};

struct read_result {
    uint32_t status;
    fidl::vector<uint8_t> buffer;
};

struct write_args {
    fidl::vector<uint8_t> buffer;
};

struct write_result {
    uint32_t status;
};

} // namespace file_native
} // namespace fdio
```

#### device.fidl-cpp-native.h

```
/* Similar to file.fidl-cpp-native.h but we derive the interface */

namespace device_native {
using namespace fidl_native;

/* Declarations for derived interface go here... */

};
```

#### ethernet.fidl-cpp-native.h

```
/* Similar to device.fidl-cpp-native.h but we have a new struct */

typedef struct ethernet_info_native {
    uint32_t feature;
    uint32_t mtu;
    uint8_t[6] mac;
    uint8_t[2]; // padding
} ethernet_info_t;
```

### Generated Idiomatic Style C++ Code

#### file.fidl-cpp.h

```
##include <fidl-cpp.h>
##include "file.fidl-cpp-native.h"

namespace fdio {

class file {
public:
    typedef std::function<void(uint32_t status)> get_class_callback;
    typedef std::function<void(uint32_t status,
    zx::vmo vmo)> get_vmo_callback;
    typedef std::function<void(uint32_t status,
    std::optional<std::vector<uint8_t>> buffer)> read_callback;
    typedef std::function<void(uint32_t status)> write_callback;

    virtual ~file() = 0;
    virtual void get_class(get_class_callback callback) = 0;
    virtual void get_vmo(get_vmo_callback callback) = 0;
    virtual void read(uint32_t size, read_callback callback) = 0;
    virtual void write(std::vector<uint8_t> buffer,
    write_callback callback callback) = 0;

    class proxy : public file {
        /* proxy stuff */
    };

    class stub : public file {
        /* stub stuff */
    };
};

typedef fidl::interface_ptr<file> file_ptr;
typedef fidl::interface_req<file> file_req;

} // namespace fdio
```

#### file.fidl-cpp.cc

```
##include "file.fidl-cpp.h"

namespace fdio {

/* definition of proxy and stub functions */

} // namespace fdio
```

#### device.fidl-cpp.h

```
/* Similar to file.fidl-cpp.h but we derive the interface */

class device : public file {
/* Declarations for derived interface go here... */
};
```

#### device.fidl-cpp.cc

```
/* Similar to file.fidl-cpp.cc */
```

#### ethernet.fidl-cpp.h

```
/* Similar to device.fidl-cpp.h but we also have a new struct here */

typedef struct ethernet_info {
    uint32_t feature;
    uint32_t mtu;
    uint8_t[6] mac;
    uint8_t[2]; // padding
} ethernet_info_t;
```

#### ethernet.fidl-cpp.cc

```
/* Similar to device.fidl-cpp.cc */
```

### Generated Dart Code

TBD

### Generated Rust Code

TBD

### Generated Go Code

TBD

## I/O Protocol Use

### C Client Code

```
ZX_status_t read_bytes(zx_handle_t channel, size_t size,
    uint8_t* dest, size_t* out_actual_bytes) {
    uint8_t wbuf[sizeof(fidl_message_header_t) +
        sizeof(fdio_file_read_args_t)];
    memset(wbufbuffer, 0, sizeof(wbuf));

    fidl_message_header_t* header = (fidl_message_header_t*)wbuf;
    header->transaction_id = 1;
    header->ordinal = fdio_FILE_ORDINAL_READ;

    fdio_file_read_args_t* args = (fdio_file_read_args_t*)(header + 1);
    args->size = size;

    uint8_t rbuf[ZX_CHANNEL_MAX_MESSAGE];
    ZX_channel_call_args_t call;
    call.wr_bytes = wbuf;
    call.wr_handles = NULL;
    call.wr_num_bytes = sizeof(wbuf);
    call.wr_num_handles = 0;
    call.rd_bytes = rbuf;
    call.rd_handles = NULL;
    call.rd_num_bytes = sizeof(rbuf);
    call.rd_num_handles = 0;
    size_t actual_bytes;
    ZX_status_t status = ZX_channel_call(channel, ZX_TIMEOUT_INFINITE,
        &call, &actual_bytes, NULL);
    if (status != ZX_NO_ERROR) return status;

    header = (fidl_message_header_t*)rbuf;
    if (actual_bytes < sizeof(fidl_message_header_t) ||
        header->ordinal != fdio_FILE_ORDINAL_READ)
        return ZX_ERR_PROTOCOL_VIOLATION;

    fdio_file_read_result_t* result =
    (fdio_file_read_result_t*)(header + 1);
    status = fidl_object_decode(fdio_FILE_READ_RESULT_ENCODING,
        result, actual_bytes - sizeof(fidl_message_header_t),
        NULL, 0);
    if (status != ZX_NO_ERROR) return status;

    *out_actual_bytes = result->buffer.size;
    memcpy(dest, result->buffer.data, *out_actual_bytes);
    return ZX_NO_ERROR;
}
```

### Native Style C++ Client Code

TBD, similar overall shape to C code other than some library conveniences

### Idiomatic Style C++ Client Code

```
void read_bytes_async(const fdio::file_ptr& file, size_t size,
        file::read_callback callback) {
    file->read(size, std::move(callback));
}
```
