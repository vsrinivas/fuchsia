
# Dart language FIDL tutorial

## About this tutorial

This tutorial describes how to make client calls and write servers in Dart
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/getting_started.md).

## Getting started

We'll use the `echo.fidl` sample that we discussed in the [FIDL Tutorial](README.md)
introduction section, by opening
[//garnet/examples/fidl/services/echo.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/examples/fidl/services/echo.fidl).

<!-- NOTE: the code snippets here need to be kept up to date manually by
     copy-pasting from the actual source code. Please update a snippet
     if you notice it's out of date. -->


```fidl
library fidl.examples.echo;

[Discoverable]
interface Echo {
  1: EchoString(string? value) -> (string? response);
};
```

## Build

The examples are in Topaz at:
[//topaz/examples/fidl/](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/)

You can build the code via the following:

```sh
# You'll need Topaz for Dart
fx set-petal topaz
# Also include garnet examples when building Topaz
fx set x64 --packages topaz/packages/default,garnet/packages/examples/fidl
fx full-build
```

## `Echo` server

The echo server implementation can be found at:
[//topaz/examples/fidl/echo_server_dart/lib/main.dart](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_server_dart/lib/main.dart).

This file implements the `main()` function and the `EchoImpl` class:

-   The `main()` function is executed when the component is loaded. `main()`
    registers the availability of the service with incoming connections from
    FIDL.
-   `EchoImpl` processes requests on the `Echo` interface. A new object is
    created for each channel.

To understand how the code works, here's a summary of what happens in the server
to execute an IPC call. We will dig into what each of these lines means, so it's
not necessary to understand all of this before you move on.

1.  **Startup.** The FIDL Shell loads the Dart runner, which starts the VM,
    loads `main.dart`, and calls `main()`.
1.  **Registration** `main()` registers `EchoImpl` to bind itself to incoming
    requests on the `Echo` interface. `main()` returns, but the program doesn't
    exit, because an [event
    loop](https://webdev.dartlang.org/articles/performance/event-loop) to handle
    incoming requests is running.
1.  **Service request.** The `Echo` server package receives a request to bind
    `Echo` service to a new channel, so it calls the `bind()` function passed in
    the previous step.
1.  **Service request.** `bind()` uses the `EchoImpl` instance.
1.  **API request.** The `Echo` server package receives a call to `echoString()`
    from the channel and dispatches it to `echoString()` in the `EchoImpl`
    object instance bound in the last step.
1.  **API request.** `echoString()` calls the given `callback()` function to
    return the response.

Now let's go through the details of how this works.

### File headers

Here are the import declarations in the Dart server implementation:

```dart
import 'package:fidl/fidl.dart';
import 'package:fuchsia.fidl.echo2/echo2.dart';
import 'package:lib.app.dart/app.dart';
```

-   `fidl.dart` exposes the FIDL runtime library for Dart. Our program needs it
    for `InterfaceRequest`.
-   `app.dart` is required for `StartupContext`, which is where we register
    our service.
-   `echo2.dart` contains bindings for the `Echo` interface.This file is
    generated from the interface defined in `echo2.fidl`.

### main()

Everything starts with main():

```dart
void main(List<String> args) {
  _context = new StartupContext.fromStartupInfo();
  _echo = new _EchoImpl();
  _context.outgoingServices.addServiceForName<Echo>(_echo.bind, Echo.$serviceName);
}
```

`main()` is called by the Dart VM when your service is loaded, similar to
`main()` in a C or C++ component. It binds an instance of `EchoImpl`, our
implementation of the `Echo` interface, to the name of the `Echo` service.

Eventually, another FIDL component will attempt to connect to our component.

### The `bind()` function

Here's what it looks like:

```dart
void bind(InterfaceRequest<Echo> request) {
  _binding.bind(this, request);
}
```

The `bind()` function is called when the first channel is received from another
component. This function binds once for each service it makes available to the
other component (remember that each service exposes a single interface). The
information is cached in a data structure owned by the FIDL runtime, and used to
create objects to be the endpoints for additional incoming channels.

Unlike C++, Dart only has a [single
thread](https://webdev.dartlang.org/articles/performance/event-loop#darts-single-thread-of-execution)
per isolate, so there's no possible confusion over which thread owns a channel.

#### Is there really only one thread?

Both yes and no. There's only one thread in your component's VM, but the
handle watcher isolate has its own, separate thread so that component isolates
don't have to block. Component isolates can also spawn new isolates, which
will run on different threads.

### The `echoString` function

Finally we reach the implementation of the server API. Your `EchoImpl` object
receives a call to the `echoString()` function. It receives the arguments to the
function, as well as a callback function to return the result parameters:

```dart
void echoString(String value, void callback(String response)) {
  print('EchoString: $value');
  callback(value);
}
```

## `Echo` client

The echo client implementation can be found at:
[//topaz/examples/fidl/echo_client_dart/lib/main.dart](https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_client_dart/lib/main.dart)

Our simple client does everything in `main()`.

**Note:** a component can be a client, a service, or both, or many. The
distinction in this example between Client and Server is purely for
demonstration purposes.

Here is the summary of how the client makes a connection to the echo service.

1.  **Startup.** The FIDL Shell loads the Dart runner, which starts the VM,
    loads `main.dart`, and calls `main()`.
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
1.  **Shutdown.** `dart_echo_client` exits.

### main()

The `main()` function in the client contains all the client code.

```dart
void main(List<String> args) {
  String server = 'fuchsia-pkg://fuchsia.com/echo_dart#meta/echo_server_dart.cmx';
  if (args.length >= 2 && args[0] == '--server') {
    server = args[1];
  }

  _context = new StartupContext.fromStartupInfo();
  final Services services = new Services();
  final LaunchInfo launchInfo = new LaunchInfo(
      url: server, directoryRequest: services.request());
  _context.launcher.createComponent(launchInfo, null);

  _echo = new EchoProxy();
  _echo.ctrl.bind(services.connectToServiceByName<Echo>(Echo.$serviceName));

  _echo.echoString('hello', (String response) {
    print('***** Response: $response');
  });
}
```

Again, remember that everything in FIDL is async. The call to
`_echo.echoString()` returns immediately and then `main()` returns. The FIDL
client library keeps its own pointer to the component object, which prevents
the component from exiting. Once the response arrives, all of the handles are
closed, and the component will terminate after the callback returns.

### Run the sample

You can run the Hello World example like this:

```sh
$ run fuchsia-pkg://fuchsia.com/echo_dart#meta/echo_client_dart.cmx
```

You do not need to specifically run the server because the call to
`connectToServiceByName()` in the client will automatically demand-load the
server.

## `Echo` across languages and runtimes
As a final exercise, you can now mix & match `Echo` clients and servers as you
see fit. Let's try having the Dart client call the C++ server (from the
[C++ version of the example](tutorial-cpp.md)).

```sh
$ run fuchsia-pkg://fuchsia.com/echo_dart#meta/echo_client_dart.cmx --server fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx
```

The Dart client will start the C++ server and connect to it. `EchoString()`
works across language boundaries, all that matters is that the ABI defined by
FIDL is observed on both ends.

