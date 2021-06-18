# Decoding syscall logs with fidlcat

This document describes the format for system call dumps written in log files that can be decoded by
fidlcat.

When an application writes logs, some low level library can also dump some of the system calls the
application is doing within the logs. If these logs are given to **fidlcat --from=dump**, fidlcat
can decode the system calls and the FIDL messages. The output of fidlcat is equal to the input
except that any syscall dumps decoded by fidlcat is replaced by the decoded version.

For example, these logs:

```
Attempting to connect to the daemon. This may take a couple seconds...
syscall 0x7ffc90bd25b8 channel_create 1623951365995328698 2821426 2821426 1 2 0
syscall 0x7ffc90bd2bb0 startup 2821426 2821426 Channel(1) dir /svc
syscall 0x7ffc90bd3bd8 channel_create 1623951365995367083 2821426 2821426 9 a 0
syscall 0x55fdf4fdb750 channel_write_etc 1623951365995377909 2821426 2821426 1 24 1
syscall 0x55fdf4fdb750 write_bytes 00 00 00 00 00 00 00 01 ab 89 f9 f2 d9 06 68 55 ff ff ff ff 00 00 00 00
syscall 0x55fdf4fdb750 write_etc_handle 0 0000000a 0000000e 4 0
syscall 0x55fdf4fdb750 write_status 1623951365995394610 0
```

Will give this fidlcat output:

```
Attempting to connect to the daemon. This may take a couple seconds...

1623951365.995329 FfxDoctor 2821426:2821426 zx_channel_create()
1623951365.995329   -> ZX_OK (out0: handle = Channel:00000001, out1: handle = Channel:00000002)

1623951365.995367 FfxDoctor 2821426:2821426 zx_channel_create()
1623951365.995367   -> ZX_OK (out0: handle = Channel:00000009, out1: handle = Channel:0000000a)

1623951365.995378 FfxDoctor 2821426:2821426 zx_channel_write_etc(
    handle: handle = Channel:00000001(dir:/svc), options: uint32 = 0)
  sent request fuchsia.overnet/HostOvernet.ConnectServiceConsumer = {
    svc: handle = Move(Channel:0000000a, ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE)
  }
1623951365.995394   -> ZX_OK
```

## Log format

To be able to be decoded by fidlcat, dumps within the logs must have a very well defined format.

All timestamps are in nano seconds. The base depends on the operating system. On Unix based systems,
the zero is the Unix epoch (midnight on January 1 1970) in UTC.

### process name

This log should be present only once per process. It defines the name fidlcat will display when a
log for this process is found.

The format is:

```
syscall |instance_id| process |process_id| |process_name|
```

For example:

```
syscall 0x7ffd6863e9c0 process 2916209 FfxDoctor
```

### startup handle

This log describes a startup handle. That is a handle which is available to the user code either
because the handle was given to the process at startup (Fuchsia case) or because the handle has a
special handling (Linux and other OS case).

The format is:

```
syscall |instance_id| startup |process_id| |thread_id| |handle_type|(|handle|) |type| |path|
```

For example:

```
syscall 0x7ffd68636f90 startup 2916209 2916210 Channel(1) dir /svc
```

### zx_channel_create

This log describes a call to **zx_channel_create** system call.

The format is:

```
syscall |instance_id| channel_create |timestamp| |process_id| |thread_id| |out0| |out1| |status|
```

The fields **out0** and **out1** are in hexadecimal without a leading 0x.

For example:

```
syscall 0x7ffd68637fd0 channel_create 1623951365995367083 2916209 2916210 9 a 0
```

### zx_channel_call, zx_channel_call_etc

This log describes a **zx_channel_call** or a **zx_channel_call_etc** system call.

The format is:

```
syscall |instance_id| channel_call |timestamp| |process_id| |thread_id| |channel| |bytes| |handles|
```

*   The field **channel** is in hexadecimal without a leading 0x.
*   The field **bytes** specifies the number of bytes to be written.
*   The field **handles** specifies the number of handles to be written.

If **bytes** or **handles** are not zero, this line will be followed by one or several lines which
define the bytes and handles. Each of these lines will have the same **instance_id**.

### zx_channel_write, zx_channel_write_etc

This log describes a **zx_channel_write** or a **zx_channel_write_etc** system call.

The format is:

```
syscall |instance_id| channel_write |timestamp| |process_id| |thread_id| |channel| |bytes| |handles|
```

*   The field **channel** is in hexadecimal without a leading 0x.
*   The field **bytes** specifies the number of bytes to be written.
*   The field **handles** specifies the number of handles to be written.

If **bytes** or **handles** are not zero, this line will be followed by one or several lines which
define the bytes and handles. Each of these lines will have the same **instance_id**.

### zx_channel_read, zx_channel_read_etc

This log describes a **zx_channel_read** or a **zx_channel_read_etc** syscall.

The format is:

syscall |instance_id| channel_read |process_id| |thread_id| |channel| |status| |bytes| |handles|

*   The field **channel** is in hexadecimal without a leading 0x.
*   The field **bytes** specifies the number of bytes to be written.
*   The field **handles** specifies the number of handles to be written.

If **bytes** or **handles** are not zero, this line will be followed by one or several lines which
define the bytes and handles. Each of these lines will have the same **instance_id**.

### call_status

This log describes the status for a **zx_channel_call** or a **zx_channel_call_etc** system call.

The format is:

```
syscall |instance_id| call_status |timestamp| |status| |bytes| |channels|
```

The **instance_id** is the same as the write part of the call.

*   The field **bytes** specifies the number of bytes to be written.
*   The field **handles** specifies the number of handles to be written.

If **bytes** or **handles** are not zero, this line will be followed by one or several lines which
define the bytes and handles. Each of these lines will have the same **instance_id**.

### write_status

This log describes the status for a **zx_channel_write** or a **zx_channel_write_etc**.

The format is:

```
syscall |instance_id| write_status |timestamp| |status|
```

### read_bytes write_bytes

**read_bytes** describes some bytes for **zx_channel_read** or for the receiving part of
**zx_channel_call** (after **call_status**).

**write_bytes** describes some bytes for **zx_channel_write** or for the sending part of
**zx_channel_call**.

This log describes up to 32 bytes of data. If a remaining size is greater or equal to 32, 32 bytes
must be present.

Bytes are specified in hexadecimal (without any leading 0x).

### read_handles write_handles

**read_handles** describes some handles for **zx_channel_read** or for the receiving part of
**zx_channel_call** (after **call_status**).

**write_handles** describes some handles for **zx_channel_write** or for the sending part of
**zx_channel_call**.

This log describes up to 8 handles of data. If a remaining size is greater or equal to 8, 8 handles
must be present.

Handles are specified in hexadecimal (without any leading 0x).

### write_etc_handle

**write_etc_handle** describes one handle for **zx_channel_write**.

The format is:

```
syscall |instance_id| write_etc_handle |operation| |handle| |rights| |type|
```

*   **operation** (0 or 1).
*   **handle** (in hexadecimal without any leading 0x).
*   **rights** (in hexadecimal without any leading 0x).
*   **type** (in decimal).

### examples

```
syscall 0x55fdf4fdb750 channel_write_etc 1623951365995377909 2821426 2821426 1 24 1
syscall 0x55fdf4fdb750 write_bytes 00 00 00 00 00 00 00 01 ab 89 f9 f2 d9 06 68 55 ff ff ff ff 00 00 00 00
syscall 0x55fdf4fdb750 write_etc_handle 0 0000000a 0000000e 4 0
syscall 0x55fdf4fdb750 write_status 1623951365995394610 0
```

```
syscall 0x7f792a884280 channel_read 1623951365995767872 2821426 2821426 2 0 24 1
syscall 0x7f792a884280 read_bytes 00 00 00 00 00 00 00 01 ab 89 f9 f2 d9 06 68 55 ff ff ff ff 00 00 00 00
syscall 0x7f792a884280 read_handles 0000000a
```

```
syscall 0x5591382e1510 channel_call 1623951365995377909 2916209 2916209 21 16 0
syscall 0x5591382e1510 write_bytes 01 00 00 00 00 00 00 01 cb 1f dd ee 2f e8 6c 52
syscall 0x5591382e1510 call_status 1623951365995394610 0 192 0
syscall 0x5591382e1510 read_bytes 01 00 00 00 00 00 00 01 cb 1f dd ee 2f e8 6c 52 03 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff
syscall 0x5591382e1510 read_bytes 38 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff 08 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff
syscall 0x5591382e1510 read_bytes 30 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff 28 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff
syscall 0x5591382e1510 read_bytes 39 35 62 61 37 32 38 66 37 62 61 65 61 34 38 33 36 39 61 64 62 32 36 38 39 34 61 30 33 65 39 65
syscall 0x5591382e1510 read_bytes 63 39 62 64 35 65 34 66 50 31 c2 60 00 00 00 00 19 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff
syscall 0x5591382e1510 read_bytes 32 30 32 31 2d 30 36 2d 31 30 54 31 35 3a 33 35 3a 34 34 2b 30 30 3a 30 30 00 00 00 00 00 00 00
```
