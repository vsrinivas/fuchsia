fidl-async-2
================

The intent of this lib is not to replace fidl-async, but rather to be used by
sysmem until both fidl-async and fidl-async-2 are essentially replaced by a lib
usable from Zircon that can serve FIDL using FIDL C++ generated code (at which
point sysmem and other FIDL-serving code in Zircon can switch directly to that).
That'll be better than both fidl-async and fidl-async-2 (easier to correctly
deal with handle lifetimes, easier async txn completion, etc).

In contrast to fidl-async, fidl-async-2 will continue to receive messages from
a FIDL channel while a previous request received on that channel is
asynchronously in progress.  While fidl-async is able to process requests
concurrently only if the requests are from separate channels, fidl-async-2 can
concurrently process multiple requests received from the same channel as well.
A per-binding concurrency_cap setting prevents this from resulting in unbounded
build-up of in-progress requests (however, this is not back-pressure on the
channel - hitting the concurrency cap intentionally fails the binding, to avoid
the possibility of getting stuck due to the concurrency cap).

SimpleBinding<> is analogous to C++ FIDL's Binding<>.

FidlServer<> is a base class for FIDL server sub-classes.  The FidlServer<>
base class manages the SimpleBinding<> and thread-safe shutdown of the FIDL
server.

FidlStruct<> can be used to automatically close all the handles in a C FIDL
struct when a FidlStruct<> instance goes out of scope.

All of this functionality should be superseded by usage of FIDL C++ generated
code in Zircon.
