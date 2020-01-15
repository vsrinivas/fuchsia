# C++ language FIDL tutorial

[TOC]

## About this tutorial

This tutorial describes how to make client calls and write servers in C++
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/docs/getting_started.md).

The [reference](#reference) section documents the high-level C++
interface bindings.

# Getting started

We'll use the `echo.test.fidl` sample that we discussed in the
[FIDL Tutorial](README.md) introduction section, by opening
[//garnet/examples/fidl/services/echo.test.fidl](/garnet/examples/fidl/services/echo.test.fidl).

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

You can build the code via the following:

```sh
fx set core.x64 --with //garnet/packages/examples:fidl
fx build
```

### Generated files

Building runs the FIDL compiler automatically.
It writes the glue code that allows the protocols to be used from different languages.
Below are the implementation files created for C++.

```
./out/default/fidling/gen/garnet/examples/fidl/services/fidl/examples/echo/cpp/fidl.cc
./out/default/fidling/gen/garnet/examples/fidl/services/fidl/examples/echo/cpp/fidl.h
```

## `Echo` server

The echo server implementation can be found at:
[//garnet/examples/fidl/echo_server_cpp/](/garnet/examples/fidl/echo_server_cpp/).

Find the implementation of the main function, and that of the `Echo` protocol.

To understand how the code works, here's a summary of what happens in the server
to execute an IPC call.

1.  Fuchsia loads the server executable, and your `main()` function starts.
1.  `main` creates an `EchoServerApp` object which will bind to the service
    protocol when it is constructed.
1.  `EchoServerApp()` registers itself with the `StartupContext` by calling
    `context->outgoing().AddPublicService<Echo>()`. It passes a lambda function
    that is called when a connection request arrives.
1.  Now `main` starts the run loop, expressed as an `async::Loop`.
1.  The run loop receives a request to connect from another component, so
    calls the lambda created in `EchoServerApp()`.
1.  That lambda binds the `EchoServerApp` instance to the request channel.
1.  The run loop receives a call to `EchoString()` from the channel and
    dispatches it to the object bound in the last step.
1.  `EchoString()` issues an async call back to the client using
    `callback(value)`, then returns to the run loop.

Let's go through the details of how this works.

### File headers

First the namespace definition. This matches the namespace defined in the FIDL
file in its "library" declaration, but that's incidental:

```cpp
namespace echo {
```

Here are the #include files used in the server implementation:

```cpp
#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include "src/lib/component/cpp/startup_context.h"
```

-   `fidl.h` contains the generated C++ definition of our `Echo` FIDL protocol.
-   `startup_context.h` is used by `EchoServerApp` to expose service implementations.

### main

Most `main()` functions for FIDL components look very similar. They create a
run loop using `async::Loop` or some other construct, and bind service
implementations. The `Loop.Run()` function enters the message loop to process
requests that arrive over channels.

Eventually, another FIDL component will attempt to connect to our component.

### The `EchoServerApp()` constructor

Note that a connection is defined as the _first_ channel with another component.
Any additional channels are not "connections".
Therefore, service registration is performed before the run loop
begins and before the first connection is made.

Here's what the `EchoServerApp` constructor looks like:

```cpp
EchoServerApp()
    : context_(fuchsia::sys::ComponentContext::CreateFromStartupInfo()) {
  context_->outgoing().AddPublicService<Echo>(
      [this](fidl::InterfaceRequest<Echo> request) {
        bindings_.AddBinding(this, std::move(request));
      });
```

The function calls `AddPublicService` once for each service it makes available
to the other component (remember that each service exposes a single
protocol). The information is cached by `StartupContext` and used to decide
which `Interface` factory to use for additional incoming channels. A new
channel is created every time someone calls `ConnectToService()` on the other
end.

If you read the code carefully, you'll see that the parameter to
`AddPublicService` is actually a lambda function that captures `this`. This
means that the lambda function won't be executed until a channel tries to bind
to the protocol, at which point the object is bound to the channel and will
receive calls from other components. Note that these calls have
thread-affinity, so calls will only be made from the same thread.

The function passed to `AddPublicService` can be implemented in different ways.
The one in `EchoServerApp` uses the same object for all channels. That's a good
choice for this case because the implementation is stateless. Other, more
complex implementations could create a different object for each channel or
perhaps re-use the objects in some sort of caching scheme.

Connections are always point to point. There are no multicast connections.

### The `EchoString()` function

Finally we reach the end of our server discussion. When the message loop
receives a message in the channel to call the `EchoString()` function in the
`Echo` protocol, it will be directed to the implementation below:

```cpp
void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
  printf("EchoString: %s\n", value->data());
  callback(std::move(value));
}
```

Here's what's interesting about this code:

-   The first parameter to `EchoString()` is a `fidl::StringPtr`. As the name
    suggests, a `fidl::StringPtr` can be null. Strings in FIDL are UTF-8.
-   The `EchoString()` function returns void because FIDL calls are
    asynchronous. Any value we might otherwise return wouldn't have anywhere to
    go.
-   The last parameter to `EchoString()` is the client's callback function. In
    this case, the callback takes a `fidl::StringPtr`.
-   `EchoServerApp::EchoString()` returns its response to the client by calling
    the callback. The callback invocation is also asynchronous, so the call
    often returns before the callback is run in the client.
-   Because the callback is async, the callback also returns void.

Any call to a protocol in FIDL is asynchronous. This is a big shift if you are
used to a procedural world where function calls return after the work is
complete. Because it's async, there's no guarantee that the call will ever
actually happen, so your callback may never be called. The remote FIDL
component might close, crash, be busy, etc.

## `Echo` client

Let's take a look at the client implementation:

[//garnet/examples/fidl/echo_client_cpp/](/garnet/examples/fidl/echo_client_cpp/)

The structure of the client is similar to that of the server, with a `main`
function and an `async::Loop`. The difference is that the client immediately
kicks off work once everything is initialized. In contrast, the server does no
work until a connection is accepted.

Note: a component can be a client, a server, or both, or many. The
distinction in this example between Client and Server is purely for
demonstration purposes.

Here is the summary of how the client makes a connection to the echo service.

1.  The shell loads the client executable and calls `main`.
1.  `main()` creates an `EchoClientApp` object to handle connecting to the
    server, calls `Start()` to initiate the connection, and then starts the
    message loop.
1.  In `Start()`, the client calls `context_->launcher()->CreateComponent`
    with the url to the server component. If the server component is not
    already running, it will be created at this point.
1.  Next, the client calls `ConnectToService()` to open a channel to the server
    component.
1.  `main` calls into `echo_->EchoString(...)` and passes the callback. Because
    FIDL IPC calls are async, `EchoString()` will probably return before the
    server processes the call.
1.  `main` then blocks on a response on the protocol.
1.  Eventually, the response arrives, and the callback is called with the
    result.

### main

main() in the client is very different from the server, as it's synchronous on
the server response.

```cpp
int main(int argc, const char** argv) {
  std::string server_url = "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx";
  std::string msg = "hello world";
  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  echo::EchoClientApp app;
  app.Start(server_url);
  app.echo()->EchoString(msg, [&loop](fidl::StringPtr value) {
    printf("***** Response: %s\n", value->data());
    loop.Quit();
  });
  return loop.Run();
}
```

### Start

`Start()` is responsible for connecting to the remote `Echo` service.

```cpp
void Start(std::string server_url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = server_url;
  launch_info.directory_request = echo_provider_.NewRequest();
  context_->launcher()->CreateComponent(std::move(launch_info),
                                          controller_.NewRequest());
  echo_provider_.ConnectToService(echo_.NewRequest().TakeChannel(),
                                  Echo::Name_);
}
```

First, `Start()` calls `CreateComponent()` to launch `echo_server`. Then, it
calls `ConnectToService()` to bind to the server's `Echo` protocol. The exact
mechanism is somewhat hidden, but the particular protocol is automatically
inferred from the type of `EchoPtr`, which is a typedef for
`fidl::InterfacePtr<Echo>`.

The second parameter to `ConnectToService()` is the service name.

Next the client calls `EchoString()` in the returned protocol. FIDL protocols
are asynchronous, so the call itself does not wait for `EchoString()` to
complete remotely before returning. `EchoString()` returns void because of the
async behavior.

Since the client has nothing to do until the server response arrives, and is
done working immediately after, `main()` then blocks using `loop.Run()`,
then exits. When the response will arrive, then the callback given to
`EchoString()`, will execute first, then `Run()` will return,
allowing `main()` to return and the program to terminate.

### Run the sample

You can run the Hello World example like this:

```sh
$ run fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx
```

You do not need to specifically run the server because the call to
`CreateComponent()` in the client will automatically launch the server.

# Reference

This section builds on the [C Language Bindings](tutorial-c.md#reference) and
reuses many of its elements where appropriate.

See [Comparing C, Low-Level C++, and High-Level C++ Language
Bindings](c-family-comparison.md) for a comparative analysis of the goals and
use cases for all the C-family language bindings.

## Design

### Goals

*   Support encoding and decoding FIDL messages with C++14.
*   Small, fast, efficient.
*   Depend only on a small subset of the standard library.
*   Minimize code expansion through table-driven encoding and decoding.
*   All code produced can be stripped out if unused.
*   Reuse encoders, decoders, and coding tables generated for C language bindings.

## Code Generator

### Mapping Declarations

#### Mapping FIDL Types to C++ Types

This is the mapping from FIDL types to C types which the code generator
produces.

FIDL                                        | High-Level C++
--------------------------------------------|----------------------------------
`bool`                                      | `bool`
`int8`                                      | `int8_t`
`uint8`                                     | `uint8_t`
`int16`                                     | `int16_t`
`uint16`                                    | `uint16_t`
`int32`                                     | `int32_t`
`uint32`                                    | `uint32_t`
`int64`                                     | `int64_t`
`uint64`                                    | `uint64_t`
`float32`                                   | `float`
`float64`                                   | `double`
`handle`, `handle?`                         | `zx::handle`
`handle<T>`,`handle<T>?`                    | `zx::T` *(subclass of zx::object<T>)*
`string`                                    | `std::string`
`string?`                                   | `fidl::StringPtr`
`vector<T>`                                 | `std::vector<T>`
`vector<T>?`                                | `fidl::VectorPtr<T>`
`array<T>:N`                                | `std::array<T, N>`
*protocol, protocol?* Protocol              | *class* ProtocolPtr
*request\<Protocol\>, request\<Protocol\>?* | `fidl::InterfaceRequest<Protocol>`
*struct* Struct                             | *class* Struct
*struct?* Struct                            | `std::unique_ptr<Struct>`
*table* Table                               | *class* Table
*union* Union                               | *class* Union
*union?* Union                              | `std::unique_ptr<Union>`
*enum* Foo                                  | *enum class Foo : data type*

#### Mapping FIDL Identifiers to C++ Identifiers

TODO: discuss reserved words, name mangling

#### Mapping FIDL Type Declarations to C++ Types

TODO: discuss generated namespaces, constants, enums, typedefs, encoding tables

## Bindings Library

### Dependencies

Depends only on Zircon system headers, libzx, and a portion of the C and C++
standard libraries.

Does not depend on libftl or libmtl.

### Code Style

To be discussed.

The bindings library could use Google C++ style to match FIDL v1.0 but
though this may ultimately be more confusing, especially given style choices in
Zircon so we may prefer to follow the C++ standard library style here as well.

### High-Level Types

TODO: adopt main ideas from FIDL 1.0

InterfacePtr<T> / interface_ptr<T>?

InterfaceRequest<T> / interface_req<T>?

async waiter

etc...

## Suggested API Improvements over FIDL v1

The FIDL v1 API for calling and implementing FIDL protocols has generally been
fairly effective so we would like to retain most of its structure in the
idiomatic FIDL v2 bindings. However, there are a few areas that could be
improved.

TODO: actually specify the intended API

### Handling Connection Errors

Handling connection errors systematically has been a cause of concern for
clients of FIDL v1 because method result callbacks and connection error
callbacks are implemented by different parts of the client program.

It would be desirable to consider an API which allows for localized handling of
connection errors at the point of method calls (in addition to protocol level
connection error handling as before).

See https://fuchsia-review.googlesource.com/#/c/23457/ for one example of how
a client would otherwise work around the API deficiency.

One approach towards a better API may be constructed by taking advantage of the
fact that std::function<> based callbacks are always destroyed even if they are
not invoked (such as when a connection error occurs). It is possible to
implement a callback wrapper which distinguishes these cases and allows clients
to handle them more systematically. Unfortunately such an approach may not be
able to readily distinguish between a connection error vs. proxy destruction.

Alternately we could wire in support for multiple forms of callbacks or for
multiple callbacks.

Or we could change the API entirely in favor of a more explicit Promise-style
mechanism.

There are lots of options here...

TBD (please feel free to amend / expand on this)
