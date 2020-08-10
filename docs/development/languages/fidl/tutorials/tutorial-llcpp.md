# Low-level C++ language FIDL tutorial

[TOC]

## About this tutorial

This tutorial describes how to make client calls and write servers in C++
using the Low-Level C++ Bindings (LLCPP).

[Getting Started](#getting-started) has a walk-through of using the bindings
with an example FIDL library. The [reference](#reference) section documents
the detailed bindings interface and design.

See [Comparing C, Low-Level C++, and High-Level C++ Language
Bindings][c-family-comparison] for a comparative analysis of the goals and
use cases for all the C-family language bindings.

Note: LLCPP is in currently in beta. The bindings are designed to exploit the
compatibility between FIDL wire-format and C++ memory layouts, and offer
precise control over allocation. As such, viewers are encouraged to
familiarize themselves with the [C Language Bindings](tutorial-c.md#reference)
and the [FIDL wire-format][wire-format]. Parts of this
tutorial assume knowledge of these related concepts.

# Getting started

Two build setups exist in the source tree: the Zircon build and the Fuchsia
build. The LLCPP code generator is not supported by the Zircon build. Therefore,
the steps to use the bindings depend on where the consumer code is located:

*   **Code is outside `zircon/`:**
    Add `//[library path]:[library name]_llcpp` to the GN dependencies e.g.
    `"//sdk/fidl/fuchsia.math:fuchsia.math_llcpp"`, and the bindings code
    will be automatically generated as part of the build.
*   **Code is inside `zircon/`:**
    Add a GN dependency of the form: `"$zx/system/fidl/[library-name]:llcpp"`,
    e.g. `"$zx/system/fidl/fuchsia-mem:llcpp"`, and the bindings code will be
    automatically generated as part of the build.

## Preliminary concepts

*   **Decoded message:**
    A FIDL message in [decoded form][wire-format-decoded]
    is a contiguous buffer that is directly accessible by reinterpreting the
    memory as the corresponding LLCPP FIDL type. That is, all pointers point
    within the same buffer, and the pointed objects are in a specific order
    defined by the FIDL wire-format. When making a call, a response buffer is
    used to decode the response message.

*   **Encoded message:**
    A FIDL message in [encoded form][wire-format-encoded]
    is an opaque contiguous buffer plus an array of handles. The buffer is
    of the same length as the decoded counterpart, but pointers are replaced
    with placeholders, and handles are moved to the accompanying array.
    When making a call, a request buffer is used to encode the request message.

*   **Message linearization:**
    FIDL messages have to be in a contiguous buffer packed according to the
    wire-format. When making a call however, the arguments to the bindings code
    and out-of-line objects are usually scattered in memory, unless careful
    attention is spent to follow the wire-format order. The process of walking
    down the tree of objects and packing them is termed *linearization*, and
    usually involves `O(message size)` copying.

*   **Message layout:**
    The in-memory layout of LLCPP structures is the same as the layout of the
    wire format. The LLCPP objects can be thought of as a view over the
    encoded message..

*   **Message ownership:**
    LLCPP objects use [`tracking_ptr`](#Pointers-and-memory-ownership) smart
    pointers to manage ownership and track whether an object is heap allocated
    and owned or user-managed and unowned.

## Generated API overview

Low-Level C++ bindings are full featured, and support control over allocation as
well as zero-copy encoding/decoding. (Note that contrary to the C bindings they
are meant to replace, the LLCPP bindings cover non-simple messages.)

Let's use this FIDL protocol as a motivating example:

```fidl
// fleet.fidl
library fuchsia.fleet;

struct Planet {
    string name;
    float64 mass;
    handle<channel> radio;
};
```

The following code is generated (simplified for readability):

```cpp
// fleet.h
struct Planet {
  fidl::StringView name;
  double mass;
  zx::channel radio;
};
```

Note that `string` maps to `fidl::StringView`, hence the `Planet` struct
will not own the memory associated with the `name` string. Rather, all strings
point within some buffer space that is managed by the bindings library, or that
the caller could customize. The same goes for the `fidl::VectorView<Planet>`
in the code below.

Continuing with the FIDL protocol:

```fidl
// fleet.fidl continued...
protocol SpaceShip {
    SetHeading(int16 heading);
    ScanForPlanets() -> (vector<Planet> planets);
    DirectedScan(int16 heading) -> (vector<Planet> planets);
    -> OnBeacon(int16 heading);
};
```

The following code is generated (simplified for readability):

```cpp
// fleet.h continued...
class SpaceShip final {
 public:
  struct SetHeadingRequest final {
    fidl_message_header_t _hdr;
    int16_t heading;
  };

  struct ScanForPlanetsResponse final {
    fidl_message_header_t _hdr;
    fidl::VectorView<Planet> planets;
  };
  using ScanForPlanetsRequest = fidl::AnyZeroArgMessage;

  struct DirectedScanRequest final {
    fidl_message_header_t _hdr;
    int16_t heading;
  };
  struct DirectedScanResponse final {
    fidl_message_header_t _hdr;
    fidl::VectorView<Planet> planets;
  };

  class SyncClient final { /* ... */ };
  class Call final { /* ... */ };
  class Interface { /* ... */ };

  static bool TryDispatch(Interface* impl, fidl_msg_t* msg, fidl::Transaction* txn);
  static bool Dispatch(Interface* impl, fidl_msg_t* msg, fidl::Transaction* txn);

  class ResultOf final { /* ... */ };
  class UnownedResultOf final { /* ... */ };
  class InPlace final { /* ... */ };

  // Generated classes for thread-safe async-capable client.
  struct AsyncEventHandlers {
    std::variant<fit::callback<void(int16_t)>,
                 fit::callback<void(fidl::DecodedMessage<OnBeaconResponse>)>>
        on_beacon;
  };
  class ScanForPlanetsResponseContext { /* ... */ };
  class DirectedScanResponseContext { /* ... */ };
  class ClientImpl { /* ... */ };
};
```

Notice that every request and response is modelled as a `struct`:
`SetHeadingRequest`, `ScanForPlanetsResponse`, etc.
In particular, `ScanForPlanets()` has a request that contains no arguments, and
we provide a special type for that, `fidl::AnyZeroArgMessage`.

Following those, there are three related concepts in the generated code:

+ [`SyncClient`](#sync-client): A class that owns a Zircon channel, providing
  methods to make requests to the FIDL server.
+ [`Call`](#static-functions): A class that contains static functions to make
  sync FIDL calls directly on an unowned channel, avoiding setting up a
  `SyncClient`. This is similar to the simple client wrappers from the C
  bindings, which take a `zx_handle_t`.
+ `Interface` and `[Try]Dispatch`: A server should implement the `Interface`
  pure virtual class, which allows `Dispatch` to call one of the defined
  handlers with a received FIDL message.

[`[Unowned]ResultOf`](#resultof-and-unownedresultof) are "scoping" classes
containing return type definitions of FIDL calls inside `SyncClient` and `Call`.
This allows one to conveniently write `ResultOf::SetHeading` to denote the
result of calling `SetHeading`.

[`InPlace`](#in_place-calls) is another "scoping" class that houses functions
to make a FIDL call with encoding and decoding performed in-place directly on
the user buffer. It is more efficient than those `SyncClient` or `Call`, but
comes with caveats. We will dive into these separately.

## Client API

### Sync client `(Protocol::SyncClient)`

The following code is generated for `SpaceShip::SyncClient`. Each FIDL method
always correspond to two overloads which differ in memory management strategies,
termed *flavors* in LLCPP: *managed flavor* and *caller-allocating flavor*.

```cpp
class SyncClient final {
 public:
  SyncClient(zx::channel channel);

  // FIDL: SetHeading(int16 heading);
  ResultOf::SetHeading SetHeading(int16_t heading);
  UnownedResultOf::SetHeading SetHeading(fidl::BytePart request_buffer, int16_t heading);

  // FIDL: ScanForPlanets() -> (vector<Planet> planets);
  ResultOf::ScanForPlanets ScanForPlanets();
  UnownedResultOf::ScanForPlanets ScanForPlanets(fidl::BytePart response_buffer);

  // FIDL: DirectedScan(int16 heading) -> (vector<Planet> planets);
  ResultOf::DirectedScan DirectedScan(int16_t heading);
  UnownedResultOf::DirectedScan DirectedScan(fidl::BytePart request_buffer, int16_t heading,
                                             fidl::BytePart response_buffer);
};
```

The one-way FIDL method `SetHeading(int16 heading)` maps to:

+ `ResultOf::SetHeading SetHeading(int16_t heading)`:
This is the *managed flavor*.
Buffer allocation for requests and responses are entirely handled within this
function, as is the case in simple C bindings. The bindings calculate a safe
buffer size specific to this call at compile time based on FIDL wire-format and
maximum length constraints. The buffers are allocated on the stack if they fit
under 512 bytes, or else on the heap. Here is an example of using it:

```cpp
// Create a client from a Zircon channel.
SpaceShip::SyncClient client(zx::channel(client_end));

// Calling |SetHeading| with heading = 42.
SpaceShip::ResultOf::SetHeading result = client.SetHeading(42);

// Check the transport status (encoding error, channel writing error, etc.)
if (result.status() != ZX_OK) {
  // Handle error...
}
```

In general, the managed flavor is easier to use, but may result in extra
allocation. See [ResultOf](#resultof-and-unownedresultof) for details on buffer
management.

+ `UnownedResultOf::SetHeading SetHeading(fidl::BytePart request_buffer, int16_t heading)`:
This is the *caller-allocating flavor*, which defers all memory allocation
responsibilities to the caller.
Here we see an additional parameter `request_buffer` which is always the first
argument in this flavor. The type `fidl::BytePart` references a buffer address
and size. It will be used by the bindings library to construct the FIDL request,
hence it must be sufficiently large.
The method parameters (e.g. `heading`) are *linearized* to appropriate locations
within the buffer. If `SetHeading` had a return value, this flavor would ask for
a `response_buffer` too, as the last argument. Here is an example of using it:

```cpp
// Call SetHeading with an explicit buffer, there are multiple ways...

// 1. On the stack
fidl::Buffer<SetHeadingRequest> request_buffer;
auto result = client.SetHeading(request_buffer.view(), 42);

// 2. On the heap
auto request_buffer = std::make_unique<fidl::Buffer<SetHeadingRequest>>();
auto result = client.SetHeading(request_buffer->view(), 42);

// 3. Some other means, e.g. thread-local storage
constexpr uint32_t request_size = fidl::MaxSizeInChannel<SetHeadingRequest>();
uint8_t* buffer = allocate_buffer_of_size(request_size);
fidl::BytePart request_buffer(/* data = */buffer, /* capacity = */request_size);
auto result = client.SetHeading(std::move(request_buffer), 42);

// Check the transport status (encoding error, channel writing error, etc.)
if (result.status() != ZX_OK) {
  // Handle error...
}

// Don't forget to free the buffer at the end if approach #3 was used...
```

> When the caller-allocating flavor is used, the `result` object borrows the
> request and response buffers (hence its type is under `UnownedResultOf`).
> Make sure the buffers outlive the `result` object.
> See [UnownedResultOf](#resultof-and-unownedresultof).

Caution: Buffers passed to the bindings must be aligned to 8 bytes. The
`fidl::Buffer` helper class does this automatically. Failure to align would
result in a run-time error.

* * * *

The two-way FIDL method
`ScanForPlanets() -> (vector<Planet> planets)` maps to:

+ `ResultOf::ScanForPlanets ScanForPlanets()`:
This is the *managed flavor*. Different from the C bindings, response arguments
are not returned via out-parameters. Instead, they are accessed through the
return value. Here is an example to illustrate:

```cpp
// It is cleaner to omit the |UnownedResultOf::ScanForPlanets| result type.
auto result = client.ScanForPlanets();

// Check the transport status (encoding error, channel writing error, etc.)
if (result.status() != ZX_OK) {
  // handle error & early exit...
}

// Obtains a pointer to the response struct inside |result|.
// This requires that the transport status is |ZX_OK|.
SpaceShip::ScanForPlanetsResponse* response = result.Unwrap();

// Access the |planets| response vector in the FIDL call.
for (const auto& planet : response->planets) {
  // Do something with |planet|...
}
```

> When the managed flavor is used, the returned object (`result` in this
> example) manages ownership of all buffer and handles, while `result.Unwrap()`
> returns a view over it. Therefore, the `result` object must outlive any
> references to the response.

+ `UnownedResultOf::ScanForPlanets ScanForPlanets(fidl::BytePart response_buffer)`:
The *caller-allocating flavor* receives the message into `response_buffer`.
Here is an example using it:

```cpp
fidl::Buffer<ScanForPlanetsResponse> response_buffer;
auto result = client.ScanForPlanets(response_buffer.view());
if (result.status() != ZX_OK) { /* ... */ }
auto response = result.Unwrap();
// |response->planets| points to a location within |response_buffer|.
```

> The buffers passed to caller-allocating flavor do not have to be initialized.
> A buffer may be re-used multiple times, as long as it is large enough for
> the calls involved.

Note: Since each `Planet` has a handle `zx::channel radio`, and the
`fidl::VectorView<Planet>` type does not own the individual `Planet` objects,
there needs to be a reliable way to capture the lifetime of those handles.
Here the return value `result` owns them, and takes care of closing them when
it goes out of scope.
If any handle is `std::move`ed away, `result` would not accidentally close it.

### Async-capable Client (`fidl::Client<Protocol>`)

This client is thread-safe and supports both synchronous and asynchronous calls
as well as asynchronous event handling. It also supports use with a
multi-threaded dispatcher.

#### Creation

A client is created with a client-end `zx::channel`, an `async_dispatcher_t*`,
an optional hook (`OnClientUnboundFn`) to be invoked when the channel is
unbound, and an optional `AsyncEventHandlers` containing hooks to
be invoked on FIDL events.

```cpp
Client<SpaceShip> client;
zx_status_t status = client.Bind(
    std::move(client_end), dispatcher,
    // OnClientUnboundFn
    [&](fidl::UnboundReason, zx_status_t, zx::channel) { /* ... */ },
    // AsyncEventHandlers
    { .on_beacon = [&](int16_t) { /* ... */ } });
```

#### Unbinding

The channel may be unbound automatically in case of the server-end being closed
or due to an invalid message being received from the server. You may also
actively unbind the channel through `client.Unbind()`.

Unbinding is thread-safe. In any of these cases, ongoing and future operations
will not cause a fatal failure, only returning `ZX_ERR_CANCELED` where
appropriate.

If you provided an unbound hook, it is executed as task on the dispatcher,
providing a reason and error status for the unbinding. You may also recover
ownership of the client end of the channel through the hook. The unbound hook is
guaranteed to be run.

#### Interaction with dispatcher

All asynchronous responses, event handling, and error handling are done through
the `async_dispatcher_t*` provided on creation of a client. With the exception
of the dispatcher being shutdown, you can expect that all hooks provided to the
client APIs will be executed on a dispatcher thread (and not nested within other
user code).

NOTE: If you shutdown the dispatcher while there are any active bindings, the
unbound hook MAY be executed on the thread executing shutdown. As such, you MUST
not take any locks which could be taken by hooks provided to `fidl::Client` APIs
while executing `async::Loop::Shutdown()/async_loop_shutdown()`. (You should
probably ensure that no locks are held around shutdown anyway since it joins all
dispatcher threads, which may take locks in user code).

#### Outgoing FIDL methods

You can invoke outgoing FIDL APIs through the `fidl::Client<SpaceShip>`
instance, e.g. `client->SetHeading(0)`. The full generated API is given below:

```cpp
class ClientImpl final {
 public:
  fidl::StatusAndError SetHeading(int16_t heading);
  fidl::StatusAndError SetHeading(fidl::BytePart _request_buffer,
                                  int16_t heading);

  fidl::StatusAndError ScanForPlanets(
      fit::callback<void(fidl::VectorView<Planet> planets)> _cb);
  fidl::StatusAndError ScanForPlanets(ScanForPlanetsResponseContext* _context);
  ResultOf::ScanForPlanets ScanForPlanets_Sync(int16_t heading);
  UnownedResultOf::ScanForPlanets ScanForPlanets_Sync(
      fidl::BytePart _response_buffer, int16_t heading);

  fidl::StatusAndError DirectedScan(fit::callback<void(fidl::VectorView<Planet> planets)> _cb);
  fidl::StatusAndError DirectedScan(DirectedScanResponseContext* _context);
  ResultOf::DirectedScan DirectedScan_Sync(int16_t heading);
  UnownedResultOf::DirectedScan DirectedScan_Sync(
      fidl::BytePart _request_buffer, int16_t heading,
      fidl::BytePart _response_buffer);
};
```

Note that the one-way and synchronous two-way FIDL methods have a similar API to
the `SyncClient` versions. Aside from one-way methods directly returning
`fidl::StatusAndError` and the added `_Sync` on the synchronous methods, the
behavior is identical.

##### Asynchronous APIs

The *managed* flavor of the asynchronous two-way APIs simply takes a
`fit::callback` hook which is executed on response in a dispatcher thread. The
returned `fidl::StatusAndError` refers just to the status of the outgoing call.

```cpp
auto status = client->DirectedScan(0, [&]{ /* ... */ });
```

The *caller-allocated* flavor enables you to provide the storage for the
callback as well as any associated state. This is done through the generated
virtual `ResponseContext` classes:

```cpp
class DirectedScanResponseContext : public fidl::internal::ResponseContext {
 public:
  virtual void OnReply(fidl::DecodedMessage<DirectedScanResponse> msg) = 0;
};
```

You can derive from this class, implementing `OnReply()` and `OnError()`
(inherited from `fidl::internal::ResponseContext`). You can then allocate an
object of this type as required, passing a pointer to it to the API. The object
must stay alive until either `OnReply()` or `OnError()` is invoked by the
`Client`.

NOTE: If the `Client` is destroyed with outstanding asynchronous transactions,
`OnError()` will be invoked for all of the associated `ResponseContext`s.

### Static functions `(Protocol::Call)`

The following code is generated for `SpaceShip::Call`:

```cpp
class Call final {
 public:
  static ResultOf::SetHeading
  SetHeading(zx::unowned_channel client_end, int16_t heading);
  static UnownedResultOf::SetHeading
  SetHeading(zx::unowned_channel client_end, fidl::BytePart request_buffer, int16_t heading);

  static ResultOf::ScanForPlanets
  ScanForPlanets(zx::unowned_channel client_end);
  static UnownedResultOf::ScanForPlanets
  ScanForPlanets(zx::unowned_channel client_end, fidl::BytePart response_buffer);

  static ResultOf::DirectedScan
  DirectedScan(zx::unowned_channel client_end, int16_t heading);
  static UnownedResultOf::DirectedScan
  DirectedScan(zx::unowned_channel client_end, fidl::BytePart request_buffer, int16_t heading,
               fidl::BytePart response_buffer);
};
```

These methods are similar to those found in `SyncClient`. However, they do not
own the channel. This is useful if one is migrating existing code from the
C bindings to low-level C++. Another use case is when implementing C APIs
which take a raw `zx_handle_t`. For example:

```cpp
// C interface which does not own the channel.
zx_status_t spaceship_set_heading(zx_handle_t spaceship, int16_t heading) {
  auto result = fuchsia::fleet::SpaceShip::Call::SetHeading(
      zx::unowned_channel(spaceship), heading);
  return result.status();
}
```

### ResultOf and UnownedResultOf

For a method named `Foo`, `ResultOf::Foo` is the return type of the *managed
flavor*. `UnownedResultOf::Foo` is the return type of the *caller-allocating
flavor*. Both types define the same set of methods:

*   `zx_status status() const` returns the transport status. it returns the
    first error encountered during (if applicable) linearizing, encoding, making
    a call on the underlying channel, and decoding the result.
    If the status is `ZX_OK`, the call has succeeded, and vice versa.
*   `const char* error() const` contains a brief error message when status is
    not `ZX_OK`. Otherwise, returns `nullptr`.
*   **(only for two-way calls)** `FooResponse* Unwrap()` returns a pointer
    to the FIDL response message. For `ResultOf::Foo`, the pointer points to
    memory owned by the result object. For `UnownedResultOf::Foo`, the pointer
    points to a caller-provided buffer. `Unwrap()` should only be called when
    the status is `ZX_OK`.

#### Allocation strategy And move semantics

`ResultOf::Foo` stores the response buffer inline if the message is guaranteed
to fit under 512 bytes. Since the result object is usually instantiated on the
caller's stack, this effectively means the response is stack-allocated when it
is reasonably small. If the maximal response size exceeds 512 bytes,
`ResultOf::Foo` instead contains a `std::unique_ptr` to a heap-allocated buffer.

Therefore, a `std::move()` on `ResultOf::Foo` may be costly if the response
buffer is inline: the content has to be copied, and pointers to out-of-line
objects have to be updated to locations within the destination object.
Consider the following snippet:

```cpp
int CountPlanets(ResultOf::ScanForPlanets result) { /* ... */ }

auto result = client.ScanForPlanets();
SpaceShip::ScanForPlanetsResponse* response = result.Unwrap();
Planet* planet = &response->planets[0];
int count = CountPlanets(std::move(result));    // Costly
// In addition, |response| and |planet| are invalidated due to the move
```

It may be written more efficiently as:

```cpp
int CountPlanets(fidl::VectorView<SpaceShip::Planet> planets) { /* ... */ }

auto result = client.ScanForPlanets();
int count = CountPlanets(result.Unwrap()->planets);
```

> If the result object need to be passed around multiple function calls,
> consider pre-allocating a buffer in the outer-most function and use the
> caller-allocating flavor.

### In-place calls {#inplace}

Both the *managed flavor* and the *caller-allocating flavor* will copy the
arguments into the request buffer. When there is out-of-line data involved,
*message linearization* is additionally required to collate them as per the
wire-format. When the request is large, these copying overhead can add up.
LLCPP supports making a call directly on a caller-provided buffer containing
a request message in decoded form, without any parameter copying. The request
is encoded in-place, hence the name of the scoping class `InPlace`.

```cpp
class InPlace final {
 public:
  static ::fidl::internal::StatusAndError
  SetHeading(zx::unowned_channel client_end,
             fidl::DecodedMessage<SetHeadingRequest> params);

  static ::fidl::DecodeResult<ScanForPlanets>
  ScanForPlanets(zx::unowned_channel client_end,
                 fidl::DecodedMessage<ScanForPlanetsRequest> params,
                 fidl::BytePart response_buffer);

  static ::fidl::DecodeResult<DirectedScan>
  DirectedScan(zx::unowned_channel client_end,
               fidl::DecodedMessage<DirectedScanRequest> params,
               fidl::BytePart response_buffer);
};
```

These functions always take a
[`fidl::DecodedMessage<FooRequest>`](#fidl_decodedmessage_t) which wraps the
user-provided buffer. To use it properly, initialize the request buffer with a
FIDL message in decoded form. *In particular, out-of-line objects have to be
packed according to the wire-format, and therefore any pointers in the message
have to point within the same buffer.*

When there is a response defined, the generated functions additionally ask for a
`response_buffer` as the last argument. The response buffer does not have to be
initialized.

```cpp
// Allocate buffer for in-place call
fidl::Buffer<SetHeadingRequest> request_buffer;
fidl::BytePart request_bytes = request_buffer.view();
memset(request_bytes.data(), 0, request_bytes.capacity());

// Manually construct the message
auto msg = reinterpret_cast<SetHeadingRequest*>(request_bytes.data());
msg->heading = 42;
// Here since our message is a simple struct,
// the request size is equal to the capacity.
request_bytes.set_actual(request_bytes.capacity());

// Wrap with a fidl::DecodedMessage
fidl::DecodedMessage<SetHeadingRequest> request(std::move(request_bytes));

// Finally, make the call.
auto result = SpaceShip::InPlace::SetHeading(channel, std::move(request));
// Check result.status(), result.error()
```

Despite the verbosity, there is actually very little work involved.
The buffer passed to the underlying `zx_channel_call` system call is in fact
`request_bytes`. The performance benefits become apparent when say the request
message contains a large inline array. One could set up the buffers once, then
make repeated calls while mutating the array by directly editing the buffer
in between.

Key Point: in-place calls only reduce overhead in the request part of the call.
Responses are already processed in-place even in the managed and
caller-allocating flavors.

## Server API

```cpp
class Interface {
 public:
  virtual void SetHeading(int16_t heading,
                          SetHeadingCompleter::Sync completer) = 0;

  class ScanForPlanetsCompleterBase {
   public:
    void Reply(fidl::VectorView<Planet> planets);
    void Reply(fidl::BytePart buffer, fidl::VectorView<Planet> planets);
    void Reply(fidl::DecodedMessage<ScanForPlanetsResponse> params);
  };

  using ScanForPlanetsCompleter = fidl::Completer<ScanForPlanetsCompleterBase>;

  virtual void ScanForPlanets(ScanForPlanetsCompleter::Sync completer) = 0;

  class DirectedScanCompleterBase {
   public:
    void Reply(fidl::VectorView<Planet> planets);
    void Reply(fidl::BytePart buffer, fidl::VectorView<Planet> planets);
    void Reply(fidl::DecodedMessage<DirectedScanResponse> params);
  };

  using DirectedScanCompleter = fidl::Completer<DirectedScanCompleterBase>;

  virtual void DirectedScan(int16_t heading, DirectedScanCompleter::Sync completer) = 0;
};

bool TryDispatch(Interface* impl, fidl_msg_t* msg, fidl::Transaction* txn);
```

The generated `Interface` class has pure virtual functions corresponding to the
method calls defined in the FIDL protocol. One may override these functions in
a subclass, and dispatch FIDL messages to a server instance by calling
`TryDispatch`.
The bindings runtime would invoke these handler functions appropriately.

```cpp
class MyServer final : fuchsia::fleet::SpaceShip::Interface {
 public:
  void SetHeading(int16_t heading,
                  SetHeadingCompleter::Sync completer) override {
    // Update the heading...
  }
  void ScanForPlanets(ScanForPlanetsCompleter::Sync completer) override {
    fidl::VectorView<Planet> discovered_planets = /* perform planet scan */;
    // Send the |discovered_planets| vector as the response.
    completer.Reply(std::move(discovered_planets));
  }
  void DirectedScan(int16_t heading, DirectedScanCompleter::Sync completer) override {
    fidl::VectorView<Planet> discovered_planets = /* perform a directed planet scan */;
    // Send the |discovered_planets| vector as the response.
    completer.Reply(std::move(discovered_planets));
  }
};
```

Each handler function has an additional last argument `completer`.
It captures the various ways one may complete a FIDL transaction, by sending a
reply, closing the channel with epitaph, etc.
For FIDL methods with a reply e.g. `ScanForPlanets`, the corresponding completer
defines up to three overloads of a `Reply()` function
(managed, caller-allocating, in-place), similar to the client side API.
The completer always defines a `Close(zx_status_t)` function, to close the
connection with a specified epitaph.

NOTE: Each `Completer` object must only be accessed by one thread at a time.
Simultaneous access from multiple threads will result in a crash.

### Responding asynchronously {#async-server}

Notice that the type for the completer `ScanForPlanetsCompleter::Sync` has
`::Sync`. This indicates the default mode of operation: the server must
synchronously make a reply before returning from the handler function.
Enforcing this allows optimizations: the bookkeeping metadata for making
a reply may be stack-allocated.
To asynchronously make a reply, one may call the `ToAsync()` method on a `Sync`
completer, converting it to `ScanForPlanetsCompleter::Async`. The `Async`
completer supports the same `Reply()` functions, and may out-live the scope of
the handler function by e.g. moving it into a lambda capture.

```cpp
void ScanForPlanets(ScanForPlanetsCompleter::Sync completer) override {
  // Suppose scanning for planets takes a long time,
  // and returns the result via a callback...
  EnqueuePlanetScan(some_parameters)
      .OnDone([completer = completer.ToAsync()] (auto planets) mutable {
        // Here the type of |completer| is |ScanForPlanetsCompleter::Async|.
        completer.Reply(std::move(planets));
      });
}
```

### Parallel message handling

NOTE: This use-case is currently possible only using the
[lib/fidl](/zircon/system/ulib/fidl) bindings.

By default, messages from a single binding are handled sequentially, i.e. a
single thread attached to the dispatcher (run loop) is woken up if necessary,
reads the message, executes the handler, and returns back to the dispatcher. The
`::Sync` completer provides an additional API, `EnableNextDispatch()`, which may
be used to selectively break this restriction. Specifically, a call to this API
will enable another thread waiting on the dispatcher to handle the next message
on the binding while the first thread is still in the handler. Note that
repeated calls to `EnableNextDispatch()` on the same `Completer` are idempotent.

```cpp
void DirectedScan(int16_t heading, ScanForPlanetsCompleter::Sync completer) override {
  // Suppose directed scans can be done in parallel. It would be suboptimal to block one scan until
  // another has completed.
  completer.EnableNextDispatch();
  fidl::VectorView<Planet> discovered_planets = /* perform a directed planet scan */;
  completer.Reply(std::move(discovered_planets));
}
```

# Reference

## Design

### Goals

*   Support encoding and decoding FIDL messages with C++17.
*   Provide fine-grained control over memory allocation.
*   More type-safety and more features than the C language bindings.
*   Match the size and efficiency of the C language bindings.
*   Depend only on a small subset of the standard library.
*   Minimize code bloat through table-driven encoding and decoding.
*   Reuse encoders, decoders, and coding tables generated for C language
    bindings.

## Pointers and memory ownership {#memory-ownership}

LLCPP objects use special smart pointers called `tracking_ptr` to keep track of memory ownership.
With `tracking_ptr`, LLCPP makes it possible for your code to easily set a value and forget
about ownership: `tracking_ptr` will take care of freeing memory when it goes out of scope.

These pointers have two states:

*   unowned (constructed from an `unowned_ptr_t`)
*   heap allocated and owned (constructed from a `std::unique_ptr`)

When the contents is owned, a `tracking_ptr` behaves like a `unique_ptr` and the pointer is
deleted on destruction. In the unowned state, `tracking_ptr` behaves like a raw pointer and
destruction is a no-op.

`tracking_ptr` is move-only and has an API closely matching `unique_ptr`.

### Types of object allocation
`tracking_ptr` makes it possible to create LLCPP objects with several allocation strategies.
The allocation strategies can be mixed and matched within the same code.

#### Heap allocation
To heap allocate objects, use the standard `std::make_unique`.

An example with an optional uint32 field represented as a `tracking_ptr`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-field" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

This applies to all union and table fields and data arrays within vectors and strings.
Vector and string data arrays must use the array specialization of `std::unique_ptr`,
which takes the element count as an argument.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```


To copy a collection to a `VectorView`, use `heap_copy_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-copy-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```


To copy a string to a `StringView`, use `heap_copy_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-copy-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

#### Allocators
FIDL provides an `Allocator` API that enables creating `tracking_ptr`s to LLCPP objects through a
number of allocation algorithms. Currently, `BufferThenHeapAllocator`, `UnsafeBufferAllocator`, and
`HeapAllocator` are available in fidl namespace.

The `BufferThenHeapAllocator` allocates from an in-band fixed-size buffer (can be used for stack
allocation), but falls back to heap allocation if the in-band buffer has been exhausted (to avoid
unnecessary unfortunate surprises).  Be aware that excessive stack usage can cause its own problems,
so consider using a buffer size that comfortably fits on the stack, or consider putting the whole
BufferThenHeapAllocator on the heap if the buffer needs to be larger than fits on the stack, or
consider using HeapAllocator.  Allocations must be assumed to be gone upon destruction of the
`BufferThenHeapAllocator` used to make them.

The `HeapAllocator` always allocates from the heap, and is unique among allocators (so far) in that
any/all of the `HeapAllocator` allocations can out-live the `HeapAllocator` instance used to make
them.

The `UnsafeBufferAllocator` is unsafe in the sense that it lacks heap failover, so risks creating
unfortunate data-dependent surprises unless the buffer size is absolutely guaranteed to be large
enough including the internal destructor-tracking overhead.  If the internal buffer is exhausted,
make<>() will panic the entire process.  Consider using `BufferThenHeapAllocator` instead.  Do not
use `UnsafeBufferAllocator` without rigorously testing that the worst-case set of cumulative
allocations made via the allocator all fit without a panic, and consider how the rigor will be
maintained as code and FIDL tables are changed.

Example:

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="allocator-field" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

The arguments to `allocator.make` are identical to the arguments to `std::make_unique`.
This also applies to VectorViews.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="allocator-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To copy a collection to a `VectorView` using an allocator, use `copy_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="copy-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a copy of a string using an allocator, use `copy_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="copy-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

#### Unowned pointers
In addition to the managed allocation strategies, it is also possible to directly
create pointers to memory unowned by FIDL. This is discouraged, as it is easy to
accidentally create use-after-free bugs. `unowned_ptr` exists to explicitly mark
pointers to FIDL-unowned memory.

The `unowned_ptr` helper is the recommended way to create `unowned_ptr_t`s,
which is more ergonomic than using the `unowned_ptr_t` constructor directly.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-ptr" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a `VectorView` from a collection using an unowned pointer to the
collection's data array, use `unowned_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a `StringView` from unowned memory, use `unowned_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

A `StringView` can also be created directly from string literals without using
`unowned_ptr`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="stringview-assign" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

## Code generator

### Mapping FIDL types to low-level C++ types

This is the mapping from FIDL types to Low-Level C++ types which the code
generator produces.

FIDL                                        | Low-Level C++
--------------------------------------------|------------------------------------------------------
`bool`                                      | `bool`, *(requires sizeof(bool) == 1)*
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
`handle<T>`,`handle<T>?`                    | `zx::T` *(subclass of zx::object\<T\>)*
`string`                                    | `fidl::StringView`
`string?`                                   | `fidl::StringView`
`vector<T>`                                 | `fidl::VectorView<T>`
`vector<T>?`                                | `fidl::VectorView<T>`
`array<T>:N`                                | `fidl::Array<T, N>`
*protocol, protocol?*                       | `zx::channel`
*request\<Protocol\>, request\<Protocol\>?* | `zx::channel`
*struct* Struct                             | *struct* Struct
*struct?* Struct                            | *struct* Struct*
*table* Table                               | *struct* Table
*union* Union                               | *struct* Union
*union?* Union                              | *struct* Union*
*union* Union                               | *struct* Union
*union?* Union                              | *struct* Union*
*enum* Foo                                  | *enum class Foo : data type*

#### fidl::StringView

Defined in [lib/fidl/llcpp/string_view.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/string_view.h)

Holds a reference to a variable-length string stored within the buffer. C++
wrapper of **fidl_string**. Does not own the memory of the contents.

`fidl::StringView` may be constructed by supplying the pointer and number of
UTF-8 bytes (excluding trailing `\0`) separately. Alternatively, one could pass
a C++ string literal, or any value which implements `[const] char* data()`
and `size()`. The string view would borrow the contents of the container.

It is memory layout compatible with **fidl_string**.

#### fidl::VectorView\<T\>

Defined in [lib/fidl/llcpp/vector_view.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/vector_view.h)

Holds a reference to a variable-length vector of elements stored within the
buffer. C++ wrapper of **fidl_vector**. Does not own the memory of elements.

`fidl::VectorView` may be constructed by supplying the pointer and number of
elements separately. Alternatively, one could pass any value which supports
[`std::data`](https://en.cppreference.com/w/cpp/iterator/data), such as a
standard container, or an array. The vector view would borrow the contents of
the container.

It is memory layout compatible with **fidl_vector**.

#### fidl::Array\<T, N\>

Defined in [lib/fidl/llcpp/array.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/array.h)

Owns a fixed-length array of elements.
Similar to `std::array<T, N>` but intended purely for in-place use.

It is memory layout compatible with FIDL arrays, and is standard-layout.
The destructor closes handles if applicable e.g. it is an array of handles.

#### Tables

The following example table will be used in this section:

```
table MyTable {
  1: uint32 x;
  2: uint32 y;
};
```

Tables can be built using the associated table builder. For `MyTable`, the associated builder
would be `MyTable::Builder` which can be used as follows:

```
MyTable table = MyTable::Builder(std::make_unique<MyTable::Frame>())
                        .set_x(std::make_unique<uint32_t>(10))
                        .set_y(std::make_unique<uint32_t(20))
                        .build();
```

`MyTable::Frame` is the table's `Frame` - essentially its internal storage. The internal storage
needs to be allocated separately from the builder because LLCPP maintains the object layout of
the underlying wire format.

In addition to assigning fields with `std::unique_ptr`, any of the allocation strategies previously
metioned can be alternatively used.

## Bindings library

### Dependencies

The low-level C++ bindings depend only on a small subset of header-only parts
of the standard library. As such, they may be used in environments where linking
against the C++ standard library is discouraged or impossible.

### Helper types

#### fidl::DecodedMessage\<T\>

Defined in [lib/fidl/llcpp/decoded_message.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/decoded_message.h)

Manages a FIDL message in [decoded form][wire-format-dual-forms].
The message type is specified in the template parameter `T`.
This class takes care of releasing all handles which were not consumed
(std::moved from the decoded message) when it goes out of scope.

`fidl::Encode(std::move(decoded_message))` encodes in-place.

#### fidl::EncodedMessage\<T\>

Defined in [lib/fidl/llcpp/encoded_message.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/encoded_message.h)
Holds a FIDL message in [encoded form][wire-format-dual-forms],
that is, a byte array plus a handle table.
The bytes part points to an external caller-managed buffer, while the handles part
is owned by this class. Any handles will be closed upon destruction.

`fidl::Decode(std::move(encoded_message))` decodes in-place.

##### Example

```cpp
zx_status_t SayHello(const zx::channel& channel, fidl::StringView text,
                     zx::handle token) {
  assert(text.size() <= MAX_TEXT_SIZE);

  // Manually allocate the buffer used for this FIDL message,
  // here we assume the message size will not exceed 512 bytes.
  uint8_t buffer[512] = {};
  fidl::DecodedMessage<example::Animal::SayRequest> decoded(
      fidl::BytePart(buffer, 512));

  // Fill in header and contents
  example::Animal::SetTransactionHeaderFor::SayRequest(&decoded);

  decoded.message()->text = text;
  // Handle types have to be moved
  decoded.message()->token = std::move(token);

  // Encode the message in-place
  fidl::EncodeResult<example::Animal::SayRequest> encode_result =
      fidl::Encode(std::move(decoded));
  if (encode_result.status != ZX_OK) {
    return encode_result.status;
  }

  fidl::EncodedMessage<example::Animal::SayRequest>& encoded =
      encode_result.message;
  return channel.write(0, encoded.bytes().data(), encoded.bytes().size(),
                       encoded.handles().data(), encoded.handles().size());
}
```

<!-- xrefs -->
[c-family-comparison]: /docs/development/languages/fidl/guides/c-family-comparison.md
[wire-format]: /docs/reference/fidl/language/wire-format
[wire-format-decoded]: /docs/reference/fidl/language/wire-format/README.md#Decoded-Messages
[wire-format-encoded]: /docs/reference/fidl/language/wire-format/README.md#Encoded-Messages
[wire-format-dual-forms]: /docs/reference/fidl/language/wire-format/README.md#Encoded-Messages#Dual-Forms_Encoded-vs-Decoded
