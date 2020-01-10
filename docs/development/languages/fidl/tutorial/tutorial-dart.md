# Dart language FIDL tutorial

[TOC]

## About this tutorial

This tutorial describes how to make client calls and write servers in Dart
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/docs/getting_started.md).

## Getting started

We'll use the `echo.test.fidl` sample that we discussed in the
[FIDL Tutorial](README.md) introduction section, by opening
[//garnet/examples/fidl/services/echo.test.fidl](/garnet/examples/fidl/services/echo.test.fidl).

```fidl
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="garnet/examples/fidl/services/echo.test.fidl" region_tag="protocol" adjust_indentation="auto" %}
```

## Build

The examples are in Topaz at:
[//topaz/examples/fidl/](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/)

You can build the code via the following:

```sh
fx set core.x64 --with //topaz/examples/fidl/echo_server_async_dart \
    --with //topaz/examples/fidl/echo_client_async_dart
fx build
```

## `Echo` server

The echo server implementation can be found at:
[//topaz/examples/fidl/echo_server_async_dart/lib/main.dart](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_server_async_dart/).

This file implements the `main()` function and the `EchoImpl` class:

-   The `main()` function is executed when the component is loaded. `main()`
    registers the availability of the service with incoming connections from
    FIDL.
-   `EchoImpl` processes requests on the `Echo` protocol. A new object is
    created for each channel.

To understand how the code works, here's a summary of what happens in the server
to execute an IPC call. We will dig into what each of these lines means, so it's
not necessary to understand all of this before you move on.

1.  **Startup.** The FIDL Shell loads the Dart runner, which starts the VM,
    loads `main.dart`, and calls `main()`.
1.  **Registration.** `main()` registers `EchoImpl` to bind itself to incoming
    requests on the `Echo` protocol. `main()` returns, but the program doesn't
    exit, because an [event
    loop](https://dart.dev/tutorials/language/futures) to handle
    incoming requests is running.
1.  **Service request.** The `Echo` server package receives a request to bind
    `Echo` service to a new channel, so it calls the `bind()` function passed in
    the previous step.
1.  **Service request.** `bind()` uses the `EchoImpl` instance.
1.  **API request.** The `Echo` server package receives a call to `echoString()`
    from the channel and dispatches it to `echoString()` in the `EchoImpl`
    object instance bound in the last step.
1.  **API request.** `echoString()` returns a future containing the response.

Now let's go through the details of how this works.

### File headers

Here are the import declarations in the Dart server implementation:

```dart
{% includecode gerrit_repo="fuchsia/topaz" gerrit_path="examples/fidl/echo_server_async_dart/lib/main.dart" region_tag="imports" adjust_indentation="auto" %}
```

-   `dart:async` Support for asynchronous programming with classes such as Future.
-   `fidl.dart` exposes the FIDL runtime library for Dart. Our program needs it
    for `InterfaceRequest`.
-   `fidl_echo` contains bindings for the `Echo` protocol. This file is
    generated from the protocol defined in `echo.fidl`.
-   `services.dart` is required for StartupContext, which is where we
    register our service.

### main()

Everything starts with main():

```dart
{% includecode gerrit_repo="fuchsia/topaz" gerrit_path="examples/fidl/echo_server_async_dart/lib/main.dart" indented_block="void main" adjust_indentation="auto" %}
```

`main()` is called by the Dart VM when your service is loaded, similar to
`main()` in a C or C++ component. It binds an instance of `EchoImpl`, our
implementation of the `Echo` protocol, to the name of the `Echo` service.

Eventually, another FIDL component will attempt to connect to our component.

### The `bind()` function

Here's what it looks like:

```dart
{% includecode gerrit_repo="fuchsia/topaz" gerrit_path="examples/fidl/echo_server_async_dart/lib/main.dart" indented_block="void bind" adjust_indentation="auto" %}
```

The `bind()` function is called when the first channel is received from another
component. This function binds once for each service it makes available to the
other component (remember that each service exposes a single protocol). The
information is cached in a data structure owned by the FIDL runtime, and used to
create objects to be the endpoints for additional incoming channels.

Unlike C++, Dart only has a [single
thread](https://dart.dev/tutorials/language/futures)
per isolate, so there's no possible confusion over which thread owns a channel.

#### Is there really only one thread?

Both yes and no. There's only one thread in your component's VM, but the
handle watcher isolate has its own, separate thread so that component isolates
don't have to block. Component isolates can also spawn new isolates, which
will run on different threads.

### The `echoString` function

Finally we reach the implementation of the server API. Your `EchoImpl` object
receives a call to the `echoString()` function. It accepts a string value
argument and it returns a Future of type String.

```dart
{% includecode gerrit_repo="fuchsia/topaz" gerrit_path="examples/fidl/echo_server_async_dart/lib/main.dart" indented_block="Future<String> echoString" adjust_indentation="auto" %}
```

## `Echo` client

The echo client implementation can be found at:
[//topaz/examples/fidl/echo_client_async_dart/lib/main.dart](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_client_async_dart/lib/main.dart)

Our simple client does everything in `main()`.

Note: a component can be a client, a service, or both, or many. The
distinction in this example between Client and Server is purely for
demonstration purposes.

Here is the summary of how the client makes a connection to the echo service.

1.  **Startup.** The FIDL Shell loads the Dart runner, which starts the VM,
    loads `main.dart`, and calls `main()`.
1.  **Launch.** The destination server if it wasn't started already.
1.  **Connect.** The destination server is specified, and we request for it to
    be started if it wasn't already.
1.  **Bind.** We bind `EchoProxy`, a generated proxy class, to the remote `Echo`
    service.
1.  **Invoke.** We invoke `echoString` with a value, and set a callback to
    handle the response.
1.  **Wait.** `main()` returns, but the FIDL run loop is still waiting for
    messages from the remote channel.
1.  **Handle result.** The result arrives, and our callback is executed,
    printing the response.
1.  **Shutdown.** `dart_echo_server` exits.
1.  **Shutdown.** `dart_echo_client` exits.

### main()

The `main()` function in the client contains all the client code.

```dart
{% includecode gerrit_repo="fuchsia/topaz" gerrit_path="examples/fidl/echo_client_async_dart/lib/main.dart" indented_block="Future<void> main" adjust_indentation="auto" %}
```

### Run the sample

You can run the Echo example like this:

```sh
$ fx shell run fuchsia-pkg://fuchsia.com/echo_client_async_dart#meta/echo_client_async_dart.cmx
```

You should see the output:

```sh
***** Response: hello
```

## `Echo` across languages and runtimes
As a final exercise, you can now mix & match `Echo` clients and servers as you
see fit. Let's try having the Dart client call the C++ server (from the
[C++ version of the example](tutorial-cpp.md)).

```sh
$ fx shell run fuchsia-pkg://fuchsia.com/echo_client_async_dart#meta/echo_client_async_dart.cmx \
    --server fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx
```

The Dart client will start the C++ server and connect to it. `EchoString()`
works across language boundaries, all that matters is that the ABI defined by
FIDL is observed on both ends.

