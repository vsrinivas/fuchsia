# Unified C++ FIDL bindings examples

This directory contains experimental example code for the nascent unified C++
bindings (fxbug.dev/60240). It follows the structure of `/examples/fidl`.
We can incubate the bindings API here.

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

After the unified bindings is sufficiently stable for general use,
we can move it to the top-level `/examples` directory.

## Client example

Note: the instruction here is adapted from the [LLCPP tutorials][llcpp-tut].
We should write a similar full-fledged tutorial for the unified bindings as it
matures.

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

[llcpp-tut]: https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/llcpp/basics/client?hl=en#run_the_client
