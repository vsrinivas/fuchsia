# Low-Level C++ Language Bindings

This document is a description of the low-level C++ implementation of the
Fuchsia Interface Definition Language (FIDL), including its libraries and
code generator.

See [Overview][fidl-overview] for more information about FIDL's overall
purpose, goals, and requirements, as well as links to related documents.

This specification builds on the [C Language Bindings](c.md) and reuses many
of its elements where appropriate.

See [Comparing C, Low-Level C++, and High-Level C++ Language Bindings](
c-family-comparison.md) for a comparative analysis of the goals and use cases
for all the C-family language bindings.

[TOC]

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

## Code Generator

### Mapping FIDL Types to Low-Level C++ Types

This is the mapping from FIDL types to low-Level C++ types which the code generator produces.

FIDL                                        | Low-Level C++
--------------------------------------------|------------------------------------------------------
`bool`                                      | `bool`, *assuming sizeof(bool) ==1*
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
`string`                                    | `fidl::StringView`
`string?`                                   | `fidl::StringView`
`vector<T>`                                 | `fidl::VectorView<T>`
`vector<T>?`                                | `fidl::VectorView<T>`
`array<T>:N`                                | `fidl::Array<T, N>`
*protocol, protocol?*                       | `zx::channel`
*request\<Protocol\>, request\<Protocol\>?* | `zx::channel`
*struct* Struct                             | *struct* Struct
*struct?* Struct                            | *struct* Struct*
*table* Table                               | (not yet supported)
*union* Union                               | *struct* Union
*union?* Union                              | *struct* Union*
*xunion* Xunion                             | *struct* Xunion
*xunion?* Xunion                            | *struct* Xunion*
*enum* Foo                                  | *enum class Foo : data type*

#### fidl::StringView

Defined in [lib/fidl/cpp/string_view.h](/zircon/system/ulib/fidl/include/lib/fidl/cpp/string_view.h)

Holds a reference to a variable-length string stored within the buffer. C++
wrapper of **fidl_string**. Does not own the memory of the contents.

It is memory layout compatible with **fidl_string**.
No constructor or destructor so this is POD.

#### fidl::VectorView\<T\>

Defined in [lib/fidl/cpp/vector_view.h](/zircon/system/ulib/fidl/include/lib/fidl/cpp/vector_view.h)

Holds a reference to a variable-length vector of elements stored within the
buffer. C++ wrapper of **fidl_vector**. Does not own the memory of elements.

It is memory layout compatible with **fidl_vector**.
No constructor or destructor so this is POD.

#### fidl::Array\<T, N\>

Defined in [lib/fidl/llcpp/array.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/array.h)

Owns a fixed-length array of elements.
Similar to `std::array<T, N>` but intended purely for in-place use.

It is memory layout compatible with FIDL arrays, and is standard-layout.
The destructor closes handles if applicable e.g. it is an array of handles.

### Mapping FIDL methods to Client API and Server Stubs

Low-Level C++ bindings extends the features of C bindings to cover non-simple
messages, while also enabling allocation control and zero-copy encoding/decoding
if desired. Let's use this FIDL protocol as a motivating example:

```fidl
// fleet.fidl
library fuchsia.fleet;

struct Planet {
    string name;
    float64 mass;
    handle<channel> radio;
};
```

The following code is generated (simplified from actual code):

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
};
```

The following code is generated (simplified from actual code):

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

  class SyncClient final { /* ... */ };
  class Call final { /* ... */ };
  class Interface { /* ... */ };

  static bool TryDispatch(Interface* impl, fidl_msg_t* msg, fidl::Transaction* txn);
  static bool Dispatch(Interface* impl, fidl_msg_t* msg, fidl::Transaction* txn);
};
```

Notice that every request and response is modelled as a `struct`.
E.g. `SetHeadingRequest`, `ScanForPlanetsResponse`, etc.
`ScanForPlanets()` has a request that contains no arguments, and
we provide a special type for that, `fidl::AnyZeroArgMessage`.

Following that, there are three related concepts in the generated code:

+ `SyncClient`: a class that owns a Zircon channel, providing methods to make
requests to the FIDL server.
+ `Call`: A class that contains static functions to make sync FIDL calls
directly on an unowned channel, avoiding setting up a `SyncClient`. This is
similar to the simple client wrappers from the C bindings, which take a
`zx_handle_t`.
+ `Interface` and `[Try]Dispatch`: A server should implement the `Interface`
pure virtual class, which allows `Dispatch` to call one of the defined handlers
with a received FIDL message.

#### Sync Client

```cpp
class SyncClient final {
 public:
  SyncClient(zx::channel channel);

  zx_status_t SetHeading(int16_t heading);
  zx_status_t SetHeading(fidl::BytePart request_buffer, int16_t heading);
  zx_status_t SetHeading(fidl::DecodedMessage<SetHeadingRequest> params);

  fidl::DecodeResult<ScanForPlanetsResponse>
  ScanForPlanets(fidl::BytePart response_buffer,
                 fidl::VectorView<Planet>* out_planets);
  fidl::DecodeResult<ScanForPlanetsResponse>
  ScanForPlanets(fidl::BytePart response_buffer);
};
```

The one-way FIDL method `SetHeading(int16 heading)` maps to three overloads:
+ `zx_status_t SetHeading(int16_t heading)`: This is called the "C flavor".
Buffer allocation for requests and responses are entirely managed by the
bindings library, as is the case in simple C bindings. The request message
may be non-simple. The response however, cannot have out-of-line objects, as
those pointers would outlive any internal buffers allocated by the function.
+ `zx_status_t SetHeading(fidl::BytePart request_buffer, int16_t heading)`:
This is the "caller-allocating flavor". Here we see an additional parameter
`request_buffer`, which references a buffer address and size. The buffer will
be used by the bindings library to construct the FIDL request, hence it must
be of sufficiently large size. The method parameters (e.g. `heading`) are copied
to appropriate locations within the buffer. If `SetHeading` had a return value,
this flavor would ask for a `response_buffer` too.
+ `zx_status_t SetHeading(fidl::DecodedMessage<SetHeadingRequest> params)`:
This is the "in-place flavor". In this flavor, the caller must construct a
`DecodedMessage` object and manually fill in all members except `_hdr`.
This flavor would perform in-place encoding of `params`.

The two-way non-simple FIDL method
`ScanForPlanets() -> (vector<Planet> planets)` maps to two overloads.
The C flavor is not generated, due to out-of-line types in the response.
+ `fidl::DecodeResult<ScanForPlanetsResponse> ScanForPlanets(fidl::BytePart response_buffer, fidl::VectorView<Planet>* out_planets)`:
The caller-allocating flavor receives the message into `response_buffer`,
decodes it, and updates `out_planets` to point to the vector living inside the
buffer. Since each `Planet` has a handle `zx::channel radio`, and the
`fidl::VectorView<Planet>` type does not own the individual `Planet` objects,
there needs to be a reliable way to capture the lifetime of those handles.
Here the return value `fidl::DecodeResult<ScanForPlanetsResponse>` owns the
handles, and takes care of closing them when it goes out of scope. Note that if
any handle is `std::move`ed away, `DecodeResult` would not accidentally close
it.
+ `fidl::DecodeResult<ScanForPlanetsResponse> ScanForPlanets(fidl::BytePart response_buffer)`:
The in-place flavor is similar to the caller-allocating flavor, except that one
has to reach into the `fidl::DecodeResult` to inspect the return values of the
FIDL call.

#### Static Functions (Call)

```cpp
class Call final {
 public:
  static zx_status_t SetHeading(zx::unowned_channel client_end,
                                int16_t heading);
  static zx_status_t SetHeading(zx::unowned_channel client_end,
                                fidl::BytePart request_buffer,
                                int16_t heading);
  static zx_status_t SetHeading(zx::unowned_channel client_end,
                                fidl::DecodedMessage<SetHeadingRequest> params);

  static fidl::DecodeResult<ScanForPlanetsResponse>
  ScanForPlanets(zx::unowned_channel client_end,
                 fidl::BytePart response_buffer,
                 fidl::VectorView<Planet>* out_planets);
  static fidl::DecodeResult<ScanForPlanetsResponse>
  ScanForPlanets(zx::unowned_channel client_end,
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
  return fuchsia::fleet::SpaceShip::Call::SetHeading(
      zx::unowned_channel(spaceship), heading);
}
```

#### Server Stub (Interface)

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
    completer.Reply(discovered_planets);
  }
};
```

Each handler function has an additional last argument `completer`.
It captures the various ways one may complete a FIDL transaction, by sending a
reply, closing the channel with epitaph, etc.
For FIDL methods with a reply e.g. `ScanForPlanets`, the corresponding completer
defines up to three overloads of a `Reply()` function
(C flavor, caller-allocating, in-place), similar to the client side API.

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
        completer.Reply(planets);
      });
}
```

## Bindings Library

### Dependencies

The low-level C++ bindings depend only on a small subset of header-only parts
of the standard library. As such, they may be used in environments where linking
against the C++ standard library is discouraged or impossible.

### Helper Types

#### fidl::DecodedMessage\<T\>

Defined in [lib/fidl/llcpp/decoded_message.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/decoded_message.h)

Manages a FIDL message in [decoded form](../reference/wire-format#Dual-Forms_Encoded-vs-Decoded).
The message type is specified in the template parameter `T`.
This class takes care of releasing all handles which were not consumed
(std::moved from the decoded message) when it goes out of scope.

`fidl::Encode(std::move(decoded_message))` encodes in-place.

#### fidl::EncodedMessage\<T\>

Defined in [lib/fidl/llcpp/encoded_message.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/encoded_message.h)
Holds a FIDL message in [encoded form](../reference/wire-format#Dual-Forms_Encoded-vs-Decoded),
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
    auto& header = decoded.message()->_hdr;
    header.transaction_id = 1;
    header.ordinal = example_Animal_Say_ordinal;

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

[fidl-overview]: ../README.md
