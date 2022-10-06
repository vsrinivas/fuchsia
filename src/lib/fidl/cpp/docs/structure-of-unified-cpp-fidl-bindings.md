# Structure of new C++ FIDL Bindings

**Author**: yifeit@google.com

**Date proposed**: Dec 3, 2021

This document describes a structural overview of the new C++ bindings: how
we can build it from parts of the existing C++ bindings, and how the API looks
like down the road for the collection of C++ bindings.

## Summary of changes

Rather than a monolithic whole, we view bindings as made up of two layers:

* The domain objects (the generated FIDL structures e.g. struct, table, ...
  **data**). The domain objects are [usable standalone][standalone-fidl-rfc].
* The messaging layer (the necessary code to speak methods over a protocol,
  receive events, .... **behavior**).

Gluing those two layers together we have a **message dispatcher**. A message
dispatcher is responsible for reading and writing messages over a transport, and
invoking the corresponding logic in the messaging layer. The message dispatcher
is not responsible for implementing encoding/decoding; that is reserved for the
domain objects and the messaging layer.

Over time, gradually move away from the "LLCPP" name, and call it "wire", e.g.
wire C++ bindings, wire types, wire messaging layer.

The new C++ bindings is an extension on top of the wire bindings, supporting
the domain types similar to those from HLCPP, henceforth termed "natural types",
over a messaging layer API that has the same shape as the wire messaging layer,
and the same thread-safety properties.

In doing so, we would allow the two set of domain objects (wire/natural) to each
further specialize in their intended niche in performance/ergonomics trade-offs,
instead of trying to design one set of domain object to satisfy conflicting
needs.


## Domain objects

The unified bindings support two types of domain objects: **wire** objects and
**natural** objects:

* **wire** objects are optimized for in-place decoding. They are layout
  compatible with the FIDL wire format. In practice, wire objects are imported
  from LLCPP generated code.
    * This allows LLCPP to be "rebranded" as the wire subset of C++ bindings.
    * They are generated in the `fuchsia_my_lib::wire` namespace.
* **natural** objects are high level domain objects that optimize for
  ergonomics. We'll generate slightly different domain object types in the
  unified bindings compared to those found in the HLCPP bindings, since we would
  like to make quite a few breaking API changes.
    * We could start prototyping the messaging layer with the HLCPP domain
      objects, then move to natural objects as they're implemented.
    * They are generated in the `fuchsia_my_lib` namespace (vs HLCPP domain
      objects which are generated in the `fuchsia::my::lib` namespace). This
      reflects our intention to users that the natural types are the default,
      since they are easier to use and reasonably performant. One should only
      turn to the wire objects when optimizing logic in the critical path, or
      when needing to precisely control memory allocation.

### Wire domain objects should not depend on HLCPP nor natural domain objects

The wire domain objects are a popular choice among drivers, some of which place
tight controls over memory allocations. By avoiding depending on HLCPP or
natural domain objects, we offer an option to driver authors to rule out FIDL
objects which implicitly allocate at build time. Those users could continue to
depend on the wire subset of the C++ bindings.

### Sharing types between wire and natural domain objects

For numerical types and types with equivalent layouts such as enums and bits, we
should use the same C++ type for wire and natural objects, since the space for
performance/ergonomics trade-offs there are negligible, and any differences are
more accidental than purposeful.

### Optional conversions to/from HLCPP

To ease migration, we may consider supporting constructing a natural object from
the corresponding HLCPP object, and conversions to the HLCPP objects. We should
not plan to do this until we have a compelling use case.

We propose externalizing all the conversion logic to a third set of libraries,
for example generated under `#include <fidl/my.library/hlcpp_migration.h>`, that
is not imported by either wire or unified libraries. This makes conversions
explicit hence marginally harder to use (e.g.
`fidl::ToHLCPP<NaturalType>(natural_types)` from library
`fuchsia_my_lib_hlcpp_migration`) but easier to implement. The purpose of these
conversion libraries is only to assist in piecewise migration. Over the long
run, users should fully migrate their applications to natural types, thus
dropping any HLCPP dependencies.

We should not generate user-defined conversion operators directly in the
definition of natural objects. Doing so implies that the natural objects code
would depend on the HLCPP domain objects code. However as discussed above, we'd
like to avoid introducing a dependency edge from wire objects to HLCPP, so we
would not be able to add those conversion operators to bits and enums. We could
imagine using `fidl::ToHLCPP` or the like for bits and enums, and conversion
constructors/operators for the rest, but that can be more confusing than always
using `fidl::ToHLCPP`.

### Dependency structure

From the perspective of build dependencies, this results in the following
conceptual dependency graph:

![Figure: natural domain objects depend on both wire bindings and HLCPP domain
objects](images/cpp-fidl-bindings-dependencies.png).

Naming-wise, we'll adopt the following:

* **wire domain objects/wire types**: the subset of C++ domain objects that are
  layout compatible with the FIDL wire format.
* **natural domain objects/natural types**: the subset of C++ domain objects
  that focus on ergonomics and have recursive ownership semantics.
* **wire bindings**: the subset of C++ FIDL bindings API that work with wire
  domain objects.
* **new C++ bindings**: the full set of C++ FIDL bindings API that support
  both wire domain objects and natural domain objects.
* **HLCPP domain objects**: the existing domain objects generated from
  `fidlgen_hlcpp`.
* **HLCPP bindings**: the existing HLCPP bindings that use HLCPP domain objects.

### New features in the natural domain objects

#### Non-resource types are copy & move constructible

In C++ it is idiomatic to use the copy constructor for deep copying, and provide
a move constructor for users to optimize away the copy.

#### Resource types are move constructible

Resource types would have a guaranteed deleted copy constructor to prevent API
breakages when it later acquires handles. Together with copy constructors above,
we can remove `fidl::Clone` from natural domain objects - the remaining use case
would be duplicating the handles within resource types, a fallible and rare
operation.

#### Struct API sketch

Do not expose raw fields directly; prefer setters instead (except when
supporting designated initializers). This has several benefits:

* We can swap out the internal representation to be more efficient without
  breaking source compatibility.
* We could add validation as fields are being set. For example, instead of
  triggering a late constraint validation failure on sending, we could panic
  from the setter in debug mode to catch some of these illegal states earlier.

```c++
Foo foo;
foo
    .set_foo(42)
    .set_bar(...);

// To support designated initializers, we could generate an auxiliary struct
// that is an aggregate, and which can be used to initialize the `Foo` struct.
// See full idea at https://godbolt.org/z/f53j3KfPG.
Foo foo{{
    // The inner brace constructs a |Foo::Storage_| which the users are
    // discouraged from spelling out directly.
    .foo = ...,
    .bar = ...
}};
```

## Messaging layer

The messaging layer builds on top of the domain objects and adds APIs to
send and receive FIDL messages over protocol endpoints.

Given the representative protocol below:

```fidl
library fuchsia.example;

type GreetError = enum {
  NOT_UNDERSTOOD = 1;
};

protocol Speak {
    Greet(struct { msg string; }) -> (struct { s int32; foo string; });
    GreetTwo(struct { msg1 string; msg2 string; }) -> (struct { s int32; foo string; });
    Ask() -> (struct { answers vector<string>; });
    OneWay(struct { a int32; });
    EmptyAck() -> ();

    TryGreet(struct { msg string; }) -> (struct { reply string; }) error GreetError;
    TryEmptyAck() -> (struct {}) error int32;

    -> OnWordSpoken(struct { word string; });
};
```

We propose the following generated New C++ bindings API.

### Client

Below are the possible APIs on an asynchronous client. In all cases, the user
may create a client like so:

```c++
#include <fidl/fuchsia.example/cpp/fidl.h>

fidl::Client<fuchsia_example::Speak> client(std::move(client_end), dispatcher);
```

#### Result callbacks vs response callbacks

In asynchronous two-way calls, the user passes a continuation in the form of a
callback. There are some choices for surfacing errors:

- **Response callbacks** are only invoked when the client receives a response
from the server. They are suitable when there is no need to propagate transport
error information on a per-call basis. The async client callbacks in HLCPP are
response callbacks.

- **Result callbacks** are invoked with a result object containing either a
success or an error. They are suitable when the user needs to propagate errors
for each FIDL call to their originators. For example, a server may need to make
another FIDL call as part of fulfilling an existing FIDL call, and need to fail
the original call in case of errors.

**Result callbacks** are the only option in the unified bindings, since they
convey strictly more information than response callbacks, and meshes better with
newer FIDL features such as unknown interactions. The user is free to return
early if they are not interested in transport errors.

#### Natural API

Overall guiding principles:

* Use `fit::result` in return values. `fit::result` is widely used in newer
  Fuchsia C++ code, and we should integrate with where the overall ecosystem is
  heading.
* Do not flatten method requests/responses into multiple arguments. For example,
  the generated method for `GreetTwo` should not take multiple input arguments
  for `msg1` and `msg2`. Rather, it should take a single struct
  (`SpeakGreetTwoRequest`) representing the request body. This makes things
  consistent when we support table and unions as top level request/responses.

#### Natural API, asynchronous

We define a `fidl::Result<MethodMarker>` templated class:

`fidl::Result<FooMethod>` represents the result of calling method `FooMethod`:

```c++
template <typename Method>
class fidl::Result { ... };
```

The precise definition that it expands to depends on the shape of the method:

* When the method does not use the error syntax:
  - When the method response has no body: `fidl::Result<FooMethod>` inherits
    `fit::result<fidl::Error>`.
  - When the method response has a body: `fidl::Result<FooMethod>` inherits
    `fit::result<fidl::Error, FooMethodPayload>`, where `fidl::Error` is a type
    representing any transport error or protocol level terminal errors such as
    epitaphs.
* When the method uses the error syntax:
  - When the method response payload is an empty struct:
    `fidl::Result<FooMethod>` inherits
    `fit::result<fidl::ErrorsIn<FooMethod>>` (see `ErrorsIn` below).
  - When the method response payload is not an empty struct:
    `fidl::Result<FooMethod>` inherits
  `fit::result<fidl::ErrorsIn<FooMethod>, FooMethodPayload>`.

`ErrorsIn` is used to implement [error folding][error-folding] of transport
and application errors, such that one may query `is_ok()` once on the result
object to determine whether the call succeeded at all layers of abstractions:

```c++
// |ErrorsIn<Method>| represents the set of all possible errors during
// |Method|:
// - Transport errors
// - Application errors in the |Method| error syntax
class fidl::ErrorsIn<fuchsia_example::Speak::TryGreet> {
 public:
  bool is_framework_error();
  fidl::Error framework_error();

  bool is_domain_error();
  fuchsia_example::GreetError domain_error();

  // Prints a description of the error.
  std::string FormatDescription();
};
```

A **result callback** has the following signature:

```c++
[] (fidl::Result<Method>& result) { ... }
```

Now we present some examples using the types and FIDL definition above:

```c++
//
// Examples without error syntax
//

// Natural API, async, response has a body.
client->Greet({std::string("hi")}).Then(
    [] (fidl::Result<fuchsia_example::Speak::Greet>& result) {
      // fidl::Result<fuchsia_example::Speak::Greet> =
      //     fit::result<
      //        fidl::Error,
      //        fuchsia_example::Speak::GreetPayload>;

      assert(result.is_ok());
      // Access the payload.
      result->foo();
      result->bar();
    });

// Natural API, async, response has no body.
client->EmptyAck().Then(
    [] (fidl::Result<fuchsia_example::Speak::Greet>& result) {
      // fidl::Result<fuchsia_example::Speak::Greet> =
      //     fit::result<fidl::Error>;

      assert(result.is_ok());
      // No payload to access...
    });

//
// Error syntax and error folding example
//

// Natural API, async, response has a body that's not empty struct
client->TryGreet({std::string("hi")}).Then(
    [] (fidl::Result<fuchsia_example::Speak::TryGreet>& result) {
      // fidl::Result<fuchsia_example::Speak::TryGreet> =
      //     fit::result<
      //         fidl::ErrorsIn<fuchsia_example::Speak::TryGreet>,
      //         fuchsia_example::SpeakTryGreetPayload>;

      // Check both transport and application error.
      if (!result.is_ok()) {
        FX_LOGS(ERROR) << "TryGreet failed: " << result.error_value();
        // Digging deeper, if desired.
        if (result.error_value().is_domain_error()) {
          FX_LOGS(ERROR) << "TryGreet failed with application error: "
                         << result.error_value().domain_error();
        }
        return;
      }
      // Access the payload...
      result->reply();
    });

// Natural API, async, response has a body that's empty struct
client->TryEmptyAck().Then(
    [] (fidl::Result<fuchsia_example::Speak::TryGreet>& result) {
      // fidl::Result<fuchsia_example::Speak::TryGreet> =
      //     fit::result<fidl::ErrorsIn<fuchsia_example::Speak::TryEmptyAck>>;

      // Check both transport and application error.
      if (!result.is_ok()) {
        FX_LOGS(ERROR) << "TryEmptyAck failed: " << result.error_value();
        return;
      }
      // No payload to access...
    });
```

#### Natural API, synchronous

For reference, there are many more uses of [async clients][hlcpp-async-clients]
compared to [sync clients][hlcpp-sync-clients] in HLCPP. Support for synchronous
clients in the natural messaging API will be hinging on strong user needs.
Nonetheless, we could imagine how a synchronous client could work, to not design
ourselves into a corner should the need for synchronous clients arise. It can
use the same `fidl::Result<Method>` treatment and error folding approach. The
main difference is the delivery of the outcome (return value in case of sync vs
callback in case of async).

```c++
// Construct a sync client.
// |fidl::SyncClient| is the natural counterpart to |fidl::WireSyncClient|.
fidl::SyncClient client(std::move(client_end));

// Natural API, sync.
fidl::Result<fuchsia_example::Speak::Greet> result =
    client->Greet({std::string("hi")});
// Alternatively, one may elide the template argument if using |fit::result|.
// Note that |fidl::Result| is just an alias.
fit::result result = client->Greet({std::string("hi")});

// Check error
bool ok = result.is_ok();
fidl::Error error = result.error_value();

// Use fields
int32_t s = result->s();
std::string foo = result->foo();

// Example for responses with absent bodies:
fit::result<fidl::Error> result = client->EmptyAck({std::string("hi")});

// Example for one way calls:
fit::result<fidl::Error> result = client->OneWay({42});

// Example for error syntax:
fit::result<fidl::ErrorsIn<TryEmptyAck>> result = client->TryEmptyAck();
```

Note that two-way methods with absent response bodies have the same return value
as one-way methods.

#### Using wire domain objects for individual calls

In order to make method calls with wire domain types, the user would write
`.wire()` in front of the method calls. `.wire()` returns a pointer to the wire
client implementation used to make calls.

#### Wire API, synchronous

```c++
fidl::SyncClient client(std::move(client_end));

// Wire API, sync
fidl::WireResult<fuchsia_example::Greet> result =
    client.wire()->Greet(fidl::StringView("hi"));

// Check error
bool ok = result.ok();
fidl::Error error = result.error();

// Use fields
int32_t s = result->s;
fidl::StringView foo = result->foo;
```

#### Wire API, asynchronous

```c++
fidl::Client client(std::move(client_end), dispatcher);

// Wire API, async
client.wire()->Greet(fidl::StringView("hi")).Then(
    [] (fidl::WireUnownedResult<fuchsia_examples::Speak::Greet>& result) {
      // Check error
      bool ok = result.ok();
      fidl::Error error = result.error();

      // Use fields
      int32_t s = result->s;
      fidl::StringView foo = result->foo;
    });
```

Note that the wire API subset should be the identical as the API found in the
LLCPP bindings, with the only difference being the additional `.wire()` to go
from a client interface speaking natural types to a client interface speaking
wire types.

### Input argument optimizations

For a function that send a domain object of type `T`:
- Take `T` if the domain object is a resource type, to always consume the
  object.
- Take `const T&` otherwise, to skip a copy.

### Out-of-scope features

#### No wire sync call flavors in C++14

C++14 does not support guaranteed return value optimization (RVO), which is
necessary in the design of wire synchronous calls to avoid any copies. We can
consider alternative approaches to supporting wire sync calls if a strong need
arises.

The practical implication is that functions like
`client.wire().sync()->Greet(...)` won't exist when compiling under C++14.

#### No caller-allocating flavors with natural objects

The use cases that call for controlling allocation don't intersect with those
served by ergonomic but less performant natural domain objects.

### Event handling

We define `fidl::Event<FooMethod>` to represent an event:

```c++
template <typename Method>
class fidl::Event<Method> { ... };
```

* When the event does not use the error syntax:
  - When the event has a body, `fidl::Event<FooMethod>` inherits
    `FooMethodPayload`, i.e. the domain object type representing the event body.
    See [fxbug.dev/90118](fxbug.dev/90118).
  - When the event has no body, `fidl::Event<FooMethod>` is empty.
* When the event uses the error syntax:
  - When the success payload is not an empty struct, `fidl::Event<FooMethod>`
    inherits `fit::result<ApplicationError, FooMethodPayload>`, where
    `ApplicationError` is the corresponding application error type for that
    event.
  - When the success payload is an empty struct, `fidl::Event<FooMethod>`
    inherits `fit::result<ApplicationError>`.

Note: an alternative to the second bullet is to simply omit the corresponding
`fidl::Event<FooMethod>` in case of events with absent bodies. Here we choose to
always leave in the argument since it matches the existing convention in LLCPP,
and could make for less special cases in typing out a list of event handlers.

We'll generate interface classes like the following:

```c++
class fidl::AsyncEventHandler<fuchsia_example::Speak> {
  virtual void OnWordSpoken(
    fidl::Event<fuchsia_example::Speak::OnWordSpoken>&) {}
};
```

If needed, the cascading inheritance pattern could be used in event handlers to
support choosing the domain object family on a per-event basis (wire or
natural):

```c++
class fidl::AsyncEventHandler<fuchsia_example::Speak> : public
    fidl:WireAsyncEventHandler<fuchsia_example::Speak> {
 public:
  // Users may override this method to receive natural types...
  virtual void OnWordSpoken(
    fidl::Event<fuchsia_example::Speak::OnWordSpoken>& event) {}

  // Or override this method to receive wire types...
  virtual void OnWordSpoken(
    fidl::WireEvent<fuchsia_example::Speak::OnWordSpoken>* event) override {
    OnWordSpoken(fidl::Event<fuchsia_example::Speak::OnWordSpoken>{
      /* Logic to convert wire types to natural types, potentially */
      /* using a decoder. */
    });
  }
};
```

### Server

We define `fidl::Request<FooMethod>` to represent the request of method
`FooMethod`:

```c++
template <typename Method>
class fidl::Request<Method> { ... };
```

* When the method request has a body, `fidl::Request<FooMethod>` inherits
  `FooMethodRequest`, i.e. the domain object type representing the request body.
* When the method has a request but the request has no body,
  `fidl::Request<FooMethod>` is empty.

We'll generate interface classes like the following:

```c++
class fidl::Server<fuchsia_example::Speak> {
  using GreetRequest = fidl::Request<fuchsia_example::Speak::Greet>;

  // fuchsia_example::Speak::GreetCompleter speaks wire and natural types
  //
  // Exposed methods:
  // completer.Reply(fuchsia_example::SpeakGreetResponse);
  // completer.wire().Reply(int32_t s, fidl::StringView foo);
  // completer.wire().buffer(MemoryResource resource).Reply(int32_t s, fidl::StringView foo);
  virtual void Greet(GreetRequest& request, GreetCompleter::Sync& completer) = 0;
};
```

Sending natural domain objects as replies and events can be supported in a way
that's similar to the client API, by adding methods that take natural types and
hiding wire APIs behind a `.wire()` accessor.

Example server:

```c++
class MyServer : public fidl::Server<fuchsia_example::Speak> {
 public:
  void Greet(GreetRequest& request, GreetCompleter::Sync& completer) {
    // Access request.
    std::string msg = request.msg();
    // Send response.
    completer.Reply({ZX_OK, msg});
  }
};
```

## Alternatives, unknowns, future work

### Unknown interaction handling considerations

[RFC-0138: Handling unknown interactions][rfc-0138] extends the result union
used in method responses with a third variant:

```
type result = union {
    1: response struct { pong Pong; };
    2: err uint32;
    3: transport_err zx.status;  // used to communication unknown method errors.
};
```

When exposing this feature in the unified bindings, we should aim for the
following to make it safe and ergonomic:

* Server authors should not be able to manually respond with the `transport_err`
  variant from inside a method handler. By definition, the method is known to
  the server if the code reaches a particular method handler.
* Client users should still be able to use `fit::result` types, which has two
  variants, even when the result union on the wire has three variants.

To achieve these, our idea is to decouple the physical shape of the result union
from the user API. On the server side, the `transport_err` is completely hidden
from the user - the binding runtime knows when to response with that error. On
the client side, the `transport_err` combined into other kinds of transport
errors.

This means for example the server completer of a flexible two-way method using
error syntax will take `fit::result<ApplicationError, FooPayload>`, and that of
a flexible two-way method not using the error syntax will simply take
`FooPayload`.

On the client side, users can ask the transport error object if the method was
unknown:

```c++
class fidl::ErrorsIn<ErrorSyntaxFlexible> {
  bool is_framework_error();
  fidl::Error framework_error();
  // Potential API:
  // framework_error().is_unknown_interaction();

  bool is_domain_error();
  fuchsia_example::GreetError domain_error();

  std::string FormatDescription();
};

client->ErrorSyntaxFlexible().Then(
    [] (fidl::Result<ErrorSyntaxFlexible>& result) {
      if (result.is_error()) {
        if (result.error_value().is_unknown_interaction()) {
          // The server doesn't recognize this flexible method.
        }
      }
    });
```

### Domain object compile time validations

Potentially worth exploring, but not part of our MVP, is the accepted [RFC-0051:
Safer structs for C++][rfc-0051] which was never implemented in HLCPP.

### Alternative client API ideas

#### Combine both wire and natural APIs onto the same client object

Earlier iterations of the design called for `fidl::Client` to support both wire
and natural flavors on the same object, via function overloading. A corner case
is that FIDL method calls with zero request arguments or with numerical request
arguments may result in identical function parameters in one-way and synchronous
functions. Because C++ doesn't support overload selection via return type, there
is no way for the user to explicitly indicate whether the natural flavor or the
wire flavor should be used.

```c++
// This is ambiguous. Should |result| contain a wire response or a natural response?
auto result = client->Ask_Sync();
// Same here.
auto result = client->OneWay(42);
```

There are a couple of solutions to this. We could use marker types to
disambiguate between those ([see source #1][godbolt-alt-client]):

```c++
auto result_of_std_string = c->Ask(Sync, WithStdTypes);
auto result_of_char_start = c->Ask(Sync, WithWireTypes);
```

This approach has the unfortunate downside of adding even more runtime
delegation and generated code: the `WithWireTypes` overload needs to forward its
arguments and return values to the wire client implementation.

We could use some base class trickery to put the wire flavors under a `LowLevel`
namespace ([see source #2][godbolt-alt-client]):

```c++
auto result_of_natural = c.Foo();
auto result_of_wire b = c.LowLevel::Foo();
```

This is feasible but requires adding `LowLevel` to the list of dangerous
identifiers for name collision handling. Note that we probably wouldn't want to
use `c.Wire::Foo()` since `Wire` is way more likely to collide with a user
defined FIDL method called `Wire` (think wiring funds or wiring memory for
example).

In contrast, as found in the earlier proposal, we could expose an accessor
`.wire()` to return a pointer to the object for making wire requests ([see
source #3][godbolt-alt-client]):

```c++
auto result_of_natural = c.Foo();
auto result_of_wire b = c.wire()->Foo();
```

Users who only wish to use the wire subset may create a
`fidl::WireClient<MyProtocol>` instead. Calling wire flavors on a unified client
is thus only reserved for cases where the majority of the calls benefit from the
ergonomics of natural objects, but some particular methods would like to use the
wire flavor for performance. Therefore, it looks like the last option is the
easiest path to disambiguate, while only slightly burdening the wire flavor
invocation (add `.wire()`).

### Receiving mixed wire/natural objects in a server

A challenge here is receiving both types of requests. Users could implement the
wire interface, then convert the arguments to natural types in a subset of
methods in that interface via an adaptor that we would provide.



<!-- link labels -->
[error-folding]: https://fxbug.dev/65489
[godbolt-alt-client]: https://godbolt.org/z/z6cTWsqe4
[hlcpp-async-clients]: https://cs.opensource.google/search?q=fidl::InterfacePtr%20-f:golden&ss=fuchsia%2Ffuchsia
[hlcpp-sync-clients]: https://cs.opensource.google/search?q=fidl::SynchronousInterfacePtr%20-f:golden&ss=fuchsia%2Ffuchsia
[rfc-0051]: /docs/contribute/governance/rfcs/0051_safer_structs_for_cpp.md
[rfc-0138]: /docs/contribute/governance/rfcs/0138_handling_unknown_interactions.md
[standalone-fidl-rfc]: /docs/contribute/governance/rfcs/0120_standalone_use_of_fidl_wire_format.md
