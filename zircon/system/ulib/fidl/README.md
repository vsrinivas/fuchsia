# The C and C++ fidl library

This library provides the runtime for FIDL C/C++ family of bindings. This primarily means the
definitions of the message encoding and decoding functions. This also includes the definitions of
fidl data types such as vectors and strings.

# Creating FIDL server connections

See `fidl::BindServer()` functions at
[server.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/server.h).

This implementation allows for multiple in-flight transactions and supports multi-threaded
dispatchers. Using the `ToAsync()` on completers will not stop this implementation from receiving
other messages on the bound channel. This is useful for implementing fully asynchronous servers and
in particular allows for FIDL
[hanging-get](/docs/development/api/fidl.md#delay-responses-using-hanging-gets) patterns to be
implemented such that a "Watch" method does not block every other transaction in the channel.

The implementation also supports synchronous multi-threaded servers via the `EnableNextDispatch()`
call on the `Sync` completer. `EnableNextDispatch()` enables another thread (on a multi-threaded
dispatcher) to handle the next message on a bound channel in parallel. More complex use-cases
combining synchronous and asynchronous behavior are also supported.
