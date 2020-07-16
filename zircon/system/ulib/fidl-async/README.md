# Fidl-async library

The fidl-async library provides facilities to create C and LLCPP-based FIDL servers,
and to bind a channel to a server implementation using a specified dispatcher.

## BindSingleInFlightOnly

Note: this function is deprecated and preserved for backwards compatibility only.
Refer to [`fidl::BindServer`](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/server.h)
for a more complete implementation.

See `fidl::BindSingleInFlightOnly()` functions at
[bind.h](/zircon/system/ulib/fidl-async/include/lib/fidl-async/cpp/bind.h).

For a given channel, this implementation supports one FIDL transaction at the time, i.e. if a
client sends 2 requests back to back without waiting for a reply to the first request, or 2+
clients make requests simultaneously, then the processing in the server will be sequential,
i.e. the server won't perform a read on the channel until any previous transaction has been replied
to with a write or the binding is closed for any reason. This behavior effectively buffers FIDL
messages in the channel until the server is ready to process the next FIDL message, this may be ok
for non-recurring very low rate messages but does not scale otherwise. Note that messages on
different channels could still be processed say if the server delays response on one channel via
`ToAsync()`.

Each handler function in the LLCPP FIDL server implementation has an additional last argument
`completer` (of type `fidl::Completer<T>::Sync`). It captures the various ways one may complete a
FIDL transaction, by sending a reply, closing the channel with epitaph, etc. To asynchronously make
a reply, one may call the `ToAsync()` method on a `Sync` completer (see
[tutorial-llcpp.md](/docs/development/languages/fidl/tutorial/tutorial-llcpp.md)). This does not
change the one-at-the-time behavior described above, but it allows for delayed responses to FIDL
requests.
