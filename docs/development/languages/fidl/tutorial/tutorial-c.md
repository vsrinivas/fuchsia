# C language FIDL tutorial

[TOC]

## About this tutorial

This tutorial describes how to make client calls and write servers in C
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/docs/getting_started.md).

# Getting started

We'll use the `echo.fidl` sample that we discussed in the [FIDL Tutorial](README.md)
introduction section, by opening
[//garnet/examples/fidl/services/echo.fidl](/garnet/examples/fidl/services/echo.fidl).

<!-- NOTE: the code snippets here need to be kept up to date manually by
     copy-pasting from the actual source code. Please update a snippet
     if you notice it's out of date. -->


```fidl
library fidl.examples.echo;

[Discoverable]
protocol Echo {
    EchoString(string? value) -> (string? response);
};
```

## Build

Use the following steps to build:

(@@@ to be completed)

## `Echo` server

The generated server code is in [//garnet/examples/fidl/echo_server_c/echo_server.c][server]:

```c
[01] // Copyright 2018 The Fuchsia Authors. All rights reserved.
[02] // Use of this source code is governed by a BSD-style license that can be
[03] // found in the LICENSE file.
[04]
[05] #include <lib/async-loop/loop.h>
[06] #include <lib/fdio/fd.h>
[07] #include <lib/fdio/fdio.h>
[08] #include <lib/fdio/directory.h>
[09] #include <lib/svc/dir.h>
[10] #include <stdio.h>
[11] #include <zircon/process.h>
[12] #include <zircon/processargs.h>
[13] #include <zircon/status.h>
[14] #include <zircon/syscalls.h>
[15]
[16] static void connect(void* context, const char* service_name,
[17]                     zx_handle_t service_request) {
[18]   printf("Incoming connection for %s.\n", service_name);
[19]   // TODO(abarth): Implement echo server once FIDL C bindings are available.
[20]   zx_handle_close(service_request);
[21] }
[22]
[23] int main(int argc, char** argv) {
[24]   zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
[25]   if (directory_request == ZX_HANDLE_INVALID) {
[26]     printf("error: directory_request was ZX_HANDLE_INVALID\n");
[27]     return -1;
[28]   }
[29]
[30]   async_loop_t* loop = NULL;
[31]   zx_status_t status =
[32]       async_loop_create(&kAsyncLoopConfigAttachToThread, &loop);
[33]   if (status != ZX_OK) {
[34]     printf("error: async_loop_create returned: %d (%s)\n", status,
[35]            zx_status_get_string(status));
[36]     return status;
[37]   }
[38]
[39]   async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
[40]
[41]   svc_dir_t* dir = NULL;
[42]   status = svc_dir_create(dispatcher, directory_request, &dir);
[43]   if (status != ZX_OK) {
[44]     printf("error: svc_dir_create returned: %d (%s)\n", status,
[45]            zx_status_get_string(status));
[46]     return status;
[47]   }
[48]
[49]   status = svc_dir_add_service(dir, "public", "fidl.examples.echo.Echo", NULL, connect);
[50]   if (status != ZX_OK) {
[51]     printf("error: svc_dir_add_service returned: %d (%s)\n", status,
[52]            zx_status_get_string(status));
[53]     return status;
[54]   }
[55]
[56]   status = async_loop_run(loop, ZX_TIME_INFINITE, false);
[57]   if (status != ZX_OK) {
[58]     printf("error: async_loop_run returned: %d (%s)\n", status,
[59]            zx_status_get_string(status));
[60]     return status;
[61]   }
[62]
[63]   svc_dir_destroy(dir);
[64]   async_loop_destroy(loop);
[65]
[66]   return 0;
[67] }
```

### main()

**main()**:
1. creates a startup handle (`[24` .. `28]`),
2. initializes the asynchronous loop (`[30` .. `37]`),
3. adds the **connect()** function to handle the echo service (`[49]`),
   and finally
4. runs the asychronous loop in the foreground via
   **async_loop_run()** (`[56]`).

When the async loop returns, we clean up (`[63]` and `[64]`) and exit.

### connect()

> The **connect()** function is waiting for `abarth` to implement it (`[19]`) :-)

## `Echo` client

(@@@ to be completed)

<!-- xrefs -->
[server]: /garnet/examples/fidl/echo_server_c/echo_server.c

