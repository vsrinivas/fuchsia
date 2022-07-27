# New C++ FIDL bindings examples

This directory contains example code for using the new C++ bindings.

To use the unified bindings for a library `//examples/fidl/fuchsia.examples`,
declare the following dependency:

```gn
deps = [
    "//examples/fidl/fuchsia.examples:fuchsia.examples_cpp",
]
```

and include the following header:

```cpp
#include <fidl/fuchsia.examples/cpp/fidl.h>
```

## Client example

<!-- TODO(fxbug.dev/103483): write full-fledged tutorial for the
bindings as it matures. -->

To run the client example, run the `echo_realm` component.
This creates the client and server component instances and routes the
capabilities:

```posix-terminal
ffx component run fuchsia-pkg://fuchsia.com/echo-cpp-client#meta/echo_realm.cm
```

Then, we can start the `echo_client` instance:

```posix-terminal
ffx component start /core/ffx-laboratory:echo_realm/echo_client
```

The server component starts when the client attempts to connect to the `Echo`
protocol. You should see the following output using `fx log`:

```none {:.devsite-disable-click-to-copy}
[echo_server][I] Running echo server
[echo_server][I] Incoming connection for fuchsia.examples.Echo
[echo_client][I] (Natural types) got response: hello
[echo_client][I] (Natural types) got response: hello
[echo_client][I] (Wire types) got response: hello
[echo_server][W] server error: FIDL endpoint was unbound due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)
```
