### Sysmem

## Sysmem Driver Environment

Sysmem initially will run in the platoform bus driver's devhost process, because
driver to driver communication is currently easiest when a driver with incoming
connections runs there.  Later on, it'll run in its own devhost.

Sysmem code must not fail the whole process, only failing/closing the specific
context that failed, because multiple clients in general will be connected to
the process that sysmem is running in (among other reasons).

## Connecting to sysmem

A child driver of the platform bus driver can connect to sysmem by the client
being instantiated with the TBD_SYSMEM protocol, which loads the sysmem client
proxy driver into the client driver's devhost process.  Or, the child driver may
also be loaded directly into the platform bus driver's devhost process.  Either
way, the client driver requests the TBD_SYSMEM protocol to get a limited C ABI
protocol that allows sending sysmem the server end of a
fuchsia.sysmem.Allocator channel.  The client driver can then make FIDL
requests using the client end of that channel.

A zircon non-driver (aka "user mode") process (such as virtcon) can connect to
sysmem by requesting fuchsia.sysmem.Allocator service (from among zircon
services), and make requests using the client end of that channel (like a normal
service request).  This is achieved via a sysmem zircon service that brokers the
FIDL protocol request through to the sysmem driver.

A garnet (etc) non-driver program can connect to sysmem by requesting
fuchsia.sysmem.Allocator service (from among garnet services), and make
requests using the client end of that channel.  This is achieved by having
svcmgr garnet code broker the sysmem protocol request through to the zircon
sysmem service which in turn sends the request to the sysmem zircon driver.

This way, all of the following categories of clients (potential participants)
can connect to sysmem:
  * Zircon drivers (must be child of platform bus driver for now)
  * Garnet+ drivers (must be child of platform bus driver for now)
  * Zircon processes (non-driver zircon processes)
  * Garnet processes (non-driver garnet processes)

Regardless of how a client connects, the client ends up with a
fuchsia.sysmem.Allocator client channel, on which FIDL requests can be made.

## Serving FIDL in sysmem driver

Fidl is served from the sysmem driver, including async request completion, by
using SimpleBinding<>, which together with FIDL C generated code acts a lot like
Binding<> does when used with FIDL C++ generated code.

## Sysmem protocol design constraints

The sysmem protocol is meant to allow clients using the generated FIDL C code to
make requests which may not immediately complete without blocking a client
thread.

The generated FIDL C code has no way to make a request that has a response
without blocking a client thread, and does not generate code to receive and
dispatch FIDL events.

For this reason, all requests that have a response are optional - there is
always a non-blocking way to achieve the same thing.  However, this means that
to avoid blocking, the client must act as a server/stub for
BufferCollectionEvents, and send the client end of a BufferCollectionEvents
channel using BufferCollection.SetEventSink().

The generated FIDL C code requires Layout="Simple", so all sysmem protocols
conform to that.  This means that any time there would normally be a nullable
field x, there's instead a "bool has_x;" immediately before that logically
nullable field x to indicate whether x is logically null (false) or logically
non-null (true).

## Sysmem protocol description

See comments in allocator_2.fidl and related files.
