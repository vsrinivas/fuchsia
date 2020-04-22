# Rust bindings

## Libraries {#libraries}

When built using the standard FIDL toolchain, all generated code for a given
`library` is placed in a corresponding Rust module. For example, given the
`library` declaration:

```fidl
library games.tictactoe;
```

All generated code is placed in a `fidl_games_tictactoe.rs` file.

The FIDL toolchain expects libraries to follow this naming scheme when referring
to some variable `Board` in an external `games.tictactoe` library, the
generated code will use `fidl_games_tictactoe::Board`.

In the Fuchsia tree, these generated FIDL modules are often bound to a shorter
name when being imported, e.g.
`use fidl_fuchsia_io2 as fio2;`, or `use fidl_fuchsia_data as fdata;`.

## Constants {#constants}

Given the constants:

```fidl
const uint8 BOARD_SIZE = 9;
const string NAME = "Tic-Tac-Toe";
```

The FIDL toolchain generates the following constants:

* `pub const BOARD_SIZE: u8`
* `pub const NAME: &str`

The correspondence between FIDL primitive types and Rust types are outlined in
[built-in types](#builtins).

## Fields

This section describes how the FIDL toolchain converts FIDL types to native
types in Rust. These types can appear as members in an aggregate type or as
parameters to a protocol method.

### Built-in types {#builtins}

Note: In Rust, the equivalent type for a nullable FIDL type `T?` is
an `Option` of the Rust type for `T`, so these are omitted from the table
below.

In the table below, when both an "owned" and "borrowed" variant are
specified, the "owned" type refers to the type that would appear in an aggregate
type (e.g. as the type of a struct field or vector element), and the "borrowed"
type refers to the type that would appear if it were used as a protocol method
parameter (from the client's perspective) or response tuple value (from the
server's perspective). The distinction between owned and borrowed exists in
order to take advantage of Rust’s ownership model. When making a request with a
parameter of type `T`, the [proxied function call](#protocols-client) does not
need to take ownership of `T` so the FIDL toolchain needs to generate a borrowed
version of `T`. Borrowed versions often use `&mut` since the type `T` may
contain handles, in which case the FIDL bindings zero out the handles when
encoding which modifies the input. Using `&mut` instead of taking ownership
allows callers to reuse the input value if it does not contain handles.

|FIDL Type|Rust Type|
|--- |--- |
|`bool`|`bool`|
|`int8`|`i8`|
|`int16`|`i16`|
|`int32`|`i32`|
|`int64`|`i64`|
|`uint8`|`u8`|
|`uint16`|`u16`|
|`uint32`|`u32`|
|`uint64`|`u64`|
|`float32`|`f32`|
|`float64`|`f64`|
|`array:N`|`&mut [T; N]` (borrowed)<br> `[T, N]` (owned)|
|`vector:N`|`&[T]` (borrowed, when T is a numeric primitive)<br> `&mut dyn ExactSizeIterator` (borrowed)<br>`Vec` (owned)|
|`string`|`&str` (borrowed)<br>`String` (owned)|
|`request`|`fidl::endpoints::ServerEnd`, where `PMarker` is the [marker type](#protocols) for this protocol.|
|`P`|`fidl::endpoints::ClientEnd` where `PMarker` is the [marker type](#protocols) for this protocol.|
|`handle`|`fidl::Handle`|
|`handle`|The corresponding handle type is used. For example,`fidl::Channel` or `fidl::Vmo`|


#### User defined types {#user-defined-types}

`bit`s,`enum`s, and `table`s are always referred to using their generated
type `T`. `struct`s and `union`s  can be either non-nullable or nullable,
and used in an owned context or borrowed context, which means that there
are four possible Rust equivalent types. For a given `struct T` or `union T`,
the types are as follows:

||owned|borrowed|
|--- |--- |--- |
|non-nullable|T|&mut T|
|nullable|Option>|Option<&mut T>|

### Request, response, and event parameters {#request-response-event-parameters}

When FIDL needs to generate a single Rust type representing the parameters to a
request, response, or event, such as for [result types](#protocols-client), it
uses the following rules:

* Multiple parameters are generated as a tuple of the parameter types
* A single parameter is generated following the usual rules described above
* An empty set of parameters is represented using the unit type `()`.

## Types {#types}

### Bits {types-bits}

Given a bits definition like:

```fidl
bits FileMode : uint16 {
    READ = 0b001;
    WRITE = 0b010;
    EXECUTE = 0b100;
};
```

The FIDL toolchain generates an equivalent set of
[bitflags](https://fuchsia-docs.firebaseapp.com/rust/bitflags/index.html)
called `FileMode` with flags `FileMode::Read`, `FileMode::Write`, and
`FileMode::Execute`. Bits members using screaming snake case are converted
to camel case in the generated Rust code.

The generated `bitflags` struct will always have the complete set of
[derive rules](#derives).

### Enums {#types-enums}

Given an enum definition like:

```fidl
enum Color {
    RED = 1;
    GREEN = 2;
    BLUE = 3;
};
```

The FIDL toolchain generates an equivalent Rust `enum` using the specified
underlying type, or `u32` if none is specified:

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(u32)]
pub enum Color {
    Red = 1,
    Green = 2,
    Blue = 3,
}
```

With the following methods:

* `from_primitive(prim: u32) -> Option<Color>`: Returns `Some` of the
  enum variant corresponding to the discriminant value if any, and
  `None` otherwise.
* `into_primitive(&self) -> u32`: Returns the underlying discriminant
  value.

The generated `enum` will always have the complete set of
[Derives](#derives).

### Structs {#types-structs}

Given a struct declaration:

```fidl
struct WebPage {
    uint32 id;
    string url = "www.example.com";
};
```

The FIDL toolchain generates an equivalent Rust `struct`:

```rust
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct WebPage {
    pub id: u32,
    pub url: String,
}
```

Note: The default values for struct members are not yet supported in
the rust bindings, and therefore do not affect the generated Rust `struct`.

The generated `WebPage` `struct` follows the [derive](#derives) rules.

### Unions {#types-unions}

Given the following union definition:

```fidl
union JsonValue {
    1: reserved;
    2: int32 int_value;
    3: string string_value;
};
```

The FIDL toolchain generates an equivalent Rust `enum`:

```rust
#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum JsonValue {
    IntValue(i32),
    StringValue(String),
}
```

The generated `JsonValue` `enum` follows the [derive](#derives) rules.

#### Flexible unions

Flexible unions (that is, unions that are prefixed with the `flexible` keyword
in their FIDL definition) have an enum additional variant generated to
represent the case where the variant is unknown. This variant is considered
private - users should instead match against it with a catch all case, which
also ensures that adding new variants to a flexible union is source compatible:

```rust
// this code will still compile if a new union variant is added
match json_value {
    JsonValue::IntValue(val) => ...,
    JsonValue::StringValue(val) => ...,
    _ => ..., // unknown variant
}
```

Encoding a union with an unknown variant will write the unknown data and the
original ordinal back onto the wire.

### Tables {#types-tables}

Given the following table definition:

```table
table User {
    1: reserved;
    2: uint8 age;
    3: string name;
};
```

The FIDL toolchain generates a `struct` `User` where each member is optional:

```rust
#[derive(Debug, PartialEq)]
pub struct User {
  pub age: Option<u8>,
  pub name: Option<String>,
}
```

With the following methods:

* `empty() -> User`: Returns a new `User`, with each member initialized to
  `None`.

The generated `User` `struct` follows the [derive](#derives) rules.

#### Table initialization {#table-initialization}

The recommended way of initializing a table is using the struct update
 syntax:

```rust
let user = User {
    age: Some(20),
    ..User::empty()
};
```
This prevents API breakage when new fields are added.

### Derives {#derives}

When the FIDL toolchain generates a new `struct` or `enum` for a FIDL type, it
attempts to `derive` as many traits from a predefined list of useful traits
as it can, including `Debug`, `Copy`, `Clone`, etc. The complete list of
default derived traits can be found in the
[compiler source](/tools/fidl/fidlgen_rust/ir/ir.go#880).

For aggregate types, such as structs, unions, and tables, the set of derives is
determined by starting with the list of all possible derives and then removing
some based on the fields that are transitively present in the type. For example,
aggregate types that transitively contain a `vector` do not derive `Copy`, and
types that contain a `handle` do not derive `Copy` and `Clone`. Additionally,
arrays larger than 32 have no derives. When in doubt, refer to the generated
code to check which traits are derived by a specific type. The complete set
of rules can be found in
[`fillDerivesForSource`](/tools/fidl/fidlgen_rust/ir/ir.go#1188).

## Protocols {#protocols}

Given a protocol:

```fidl
protocol TicTacToe {
    StartGame(bool start_first);
    MakeMove(uint8 row, uint8 col) -> (bool success, GameState? new_state);
    -> OnOpponentMove(GameState new_state);
};
```

The main entrypoint for interacting with `TicTacToe` is the `TicTacToeMarker`
struct, which contains two associated types:

* `Proxy`: The associated proxy type for use with async clients. In this
  example, this is a generated `TicTacToeProxy` type. Synchronous clients
  should use `TicTacToeSynchronousProxy` directly (see
  [Synchronous](#protocols-client-synchronous)).
* `RequestStream`: The associated request stream that servers implementing
  this protocol will need to handle. In this example, this is
  `TicTacToeRequestStream`, which is generated by FIDL.
   Additionally, `TicTacToeMarker` has the following associated constants:
* `DEBUG_NAME: &’static str`: The name of the service suitable for debug purposes

Other code may be generated depending on the
[Protocol and method attributes](#protocol-method-attributes) applied to the
protocol or its methods.

### Client {#protocols-client}

#### Asynchronous {#protocols-client-asynchronous}

For asynchronous clients, the FIDL toolchain generates a `TicTacToeProxy`
struct with the following associated types:

Associated types:

* `TicTacToeProxy::MakeMoveResponseFut`: The `Future` type for the response
  of a two way method. This type implements
  `std::future::Future<Output = Result<(bool, Option<Box<GameState>>), fidl::Error>> + Send`.
* `FooProxy::OnOpponentMoveResponseFut`: The `Future` type for an incoming
  event. This type implements
  `std::future::Future<Output = Result<GameState, fidl::Error>> + Send`

Methods:

* `new(channel: fidl::AsyncChannel) -> TicTacToeProxy`: Create a new proxy
  for `TicTacToe`.
* `into_channel(self) -> Result<fidl::AsyncChannel>`: Attempt to convert the
  proxy back into a channel.
* `take_event_stream(&self) -> TicTacToeEventStream`: Get a `Stream` of events
  from the server end.
* `start_game(&self, mut start_first: bool) -> Result<(), fidl::Error>`: Proxy
  method for a fire and forget protocol method. It takes as arguments the
  request parameters and returns an empty result.
* `make_move(&self, mut row: u8, mut col: u8) -> Self::MakeMoveResponseFut`:
  Proxy method for a two way method. It takes as arguments the request
  parameters and returns a `Future` of the response.

An example of setting up an asynchronous proxy is available in the Rust tutorial.

#### Synchronous {#protocols-client-synchronous}

For synchronous clients of the `TicTacToe` protocols, the FIDL toolchain
generates a `TicTacToeSynchronousProxy` struct with the following methods:

* `new(channel: fidl::Channel) -> TicTacToeSynchronousProxy`: Returns a new
  synchronous proxy over the client end of a channel. The server end is
  assumed to implement the `TicTacToe` protocol.
* `into_channel(self) -> fidl::Channel`: Convert the proxy back into a channel.
* `start_game(&mut self, mut a: i64) -> Result<(), fidl::Error>`: Proxy
  method for a fire and forget method: it takes the request parameters as
  arguments and returns an empty result.
* `make_move(&mut self, mut row: u8, mut col: u8, [__deadline: zx::Time](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/src/lib/zircon/rust/src/time.rs)) -> Result<(bool, Option<Box<GameState>>), fidl::Error>`:
  Proxy method for a two way method. It takes the request parameters as
  arguments followed by a deadline parameter which dictates how long the
  method call will wait for a response (or `zx::Time::INFINITE` to block
  indefinitely). This method returns a result of the response parameters
  as a tuple.

An example of setting up a synchronous proxy is available in the Rust tutorial.

### Server {#protocols-server}

#### Protocol request stream {#protocol-request-stream}

To represent the stream of incoming requests to a server, the FIDL toolchain
generates a `TicTacToeRequestStream` type that implements
`futures::Stream<Item = Result<TicTacToeRequest, fidl::Error>>` as well as
`fidl::endpoints::RequestStream`. Each protocol has a corresponding request
stream type.

#### Request enum {#request-enum}

`TicTacToeRequest` is an enum representing the possible requests of the
`Foo` protocol. It has the following variants:

* `StartGame { start_first: bool, control_handle: TicTacToeControlHandle }`:
  A fire and forget request, which contains the request parameters and a
  control handle.
* `MakeMove { row: u8, col: u8, responder: TicTacToeMakeMoveResponder }`: A
  two way method request, which contains the request parameters and a
  responder.

One such enum is generated for each protocol.

#### Request responder {#request-responder}

Each two way method has a corresponding generated responder type which the
server uses to respond to a request. In this example, which only has one two
way method, the FIDL toolchain generates `TicTacToeMakeMoveResponder`
(which follows the naming scheme `[Protocol][Method]Responder`), which
provides the following methods:

* `send(self, mut success: bool, mut new_state: Option<&mut GameState>) -> Result<(), fidl::Error>`:
  Sends a response.
* `send_no_shutdown_on_err(self, mut success: bool, mut new_state: Option<&mut GameState>) -> Result<(), fidl::Error>`:
  Similar to `send` but does not shut down the channel if an error occurs.
* `control_handle(&self) -> &TicTacToeControlHandle`: Get the underlying
  [control handle](#protocol-control-handle).
* `drop_without_shutdown(mut self)`: Drop the Responder without shutting
  down the channel.

#### Protocol control handle {#protocol-control-handle}

The FIDL toolchain generates `TicTacToeControlHandle` to encapsulate the
client endpoint of the `Foo` protocol on the server side. It contains
the following methods:

* `shutdown(&self)`: Shut down the channel.
* `shutdown_with_epitaph(&self, status: zx_status::Status)`: Send an
  epitaph and then shut down the channel.
* `send_on_opponent_move(&self, mut new_state: &mut GameState) -> Result<(), fidl::Error>`:
  Proxy method for an event, which takes as arguments the event’s parameters
  and returns an empty result.

### Events {#protocols-events}

#### Client {#protocols-events-client}

For receiving events on the client, the FIDL toolchain generates a
`TicTacToeEventStream`, which can be obtained using the `take_event_stream()`
method on the [`FooProxy`](#protocols-client-asynchronous).
`TicTacToeEventStream` implements
`futures::Stream<Item = Result<TicTacToeEvent, fidl::Error>>`.

`TicTacToeEvent` is an enum representing the possible events. It has the
following variants:

* `OnOpponentMove { new_state: GameState }`: Discriminant for the
  `FooEvent` event.

And provides the following methods:

* `into_on_opponent_move(self) -> Option<GameState>`: Return `Some` of the
  [parameters](#request-response-event-parameters) of the event, or `None`
  if the variant does not match the method call.

#### Server {#protocols-events-server}

Servers can send events by using the [control handle](#protocol-control-handle)
corresponding to the protocol. The control handle can be obtained through a
`TicTacToeRequest` received from the client. For fire and forget methods, the
control handle is available through the `control_handle` field, and for two
way methods, it is available through the `control_handle()` method on the
responder. A control handle for a protocol can also be obtained through the
corresponding request stream (in this example, `TicTacToeRequestStream`), since
it implements `fidl::endpoints::RequestStream`.

### Results {#protocols-results}

For a method with an error type:

```fidl
protocol TicTacToe {
    MakeMove(uint8 row, uint8 col) -> (GameState new_state) error MoveError;
};
```

The FIDL toolchain will generate a public `TicTacToeMakeMoveResult` type alias
for `std::result::Result<GameState, MoveError>`. The rest of the bindings code
for this method will be generated as if it has a single response parameter
`result` of type `TicTacToeMakeMoveResult`. The type used for a successful
result follows the
[parameter type conversion rules](#request-response-event-parameters).

### Protocol composition {#protocol-composition}

FIDL does not have a concept of inheritance, and generates full code as
described above for all composed protocols. In other words, the code
generated for the following:

```fidl
protocol A {
    Foo();
};

protocol B {
    compose A;
    Bar();
};
```

Is the same as the following code:

```fidl
protocol A {
    Foo();
};

protocol B {
    Foo();
    Bar();
};
```

### Protocol and method attributes {#protocol-method-attributes}

#### Transitional

The `"Transitional"` attribute does not affect the generated rust code.
Protocols that need to be transitioned can have implementation servers
temporarily use a catch-all match arm in the `Request` handler.

#### Discoverable

For protocols annotated with the `"Discoverable"` attribute, the Marker
type additionally implements the `fidl::endpoints::DiscoverableService` trait.
