# HLCPP bindings

## Libraries {#libraries}

All generated code for a given fidl `library` is placed in a corresponding C++
namespace. For example, given the `library` declaration:

```fidl
library games.tictactoe;
```

All declarations in the file reside in the `games::tictactoe` namespace, and
[test scaffolding](#test-scaffolding) will be generated in
`games::tictactoe::testing`.

## Constants {#constants}

All primitives are generated as a `constexpr`. For example, the following
 constants:

```fidl
const uint8 BOARD_SIZE = 9;
const string NAME = "Tic-Tac-Toe";
```

Are generated in the header file as:

```
constexpr uint8_t BOARD_SIZE = 42u;
extern const char[] NAME;
```

The correspondence between FIDL primitive types and C++ types is outlined in
[built-in types](#builtins). Instead of `constexpr`, `string`s are declared as an
`extern const char[]` in the header file, and defined in a `.cc` file.

## Fields {#fields}

This section describes how the FIDL toolchain converts FIDL types to native
types in HLCPP. These types can appear as members in an aggregate type or
as parameters to a protocol method.

### Built-in types {#builtins}

The FIDL types are converted to C++ types based on the following table:

|FIDL Type|HLCPP Type|
|--- |--- |
|`bool`|`bool`|
|`int8`|`int8_t`|
|`int16`|`int16_t`|
|`int32`|`int32_t`|
|`int64`|`int64_t`|
|`uint8`|`uint8_t`|
|`uint16`|`uint16_t`|
|`uint32`|`uint32_t`|
|`uint64`|`uint64_t`|
|`float32`|`float`|
|`float64`|`double`|
|`array:N`|`std::array`|
|`vector:N`|`std::vector`|
|`vector:N?`|`fidl::VectorPtr`|
|`string`|`std::string`|
|`string?`|`fidl::StringPtr`|
|`request` and `request?`|`fidl::InterfaceRequest`|
|`P`and `P?`|`fidl::InterfaceHandle`|
|`handle` and `handle?`|`zx::handle`|
|`handle` and `handle?`|The corresponding zx type is used. For example, `zx::vmo` or `zx::channel`.|

### User defined types {#user-defined-types}

In HLCPP, a user defined type (bits, enum, constant, struct, union, or table)
is referred to using the generated class or variable (see
[Type Definitions](#type-definitions)).
For a nullable user-defined type `T`, a `unique_ptr<T>` is used.

### Request, response, and event parameters {#request-response-event-parameters}

Whenever FIDL needs to generate a single type representing parameters for a
request, response, or event (e.g. when generating
[`fit::result` compatible result types](#protocols-results)), it uses the
following rules:

* Multiple arguments are generated as an `std::tuple` of the parameter types.
* A single parameter is generated following the usual rules described above.
* An empty set of parameters is represented using `void`.

## Type definitions {#type-definitions}

### Bits {#bits}

Given a bits definition like:

```fidl
bits FileMode : uint16 {
    READ = 0b001;
    WRITE = 0b010;
    EXECUTE = 0b100;
};
```
The FIDL toolchain generates an C++ `enum class` using the specified underlying
type, or `uint32_t` if none is specified:

```c++
enum class FileMode : uint16_t {
    READ = 1u;
    WRITE = 2u;
    EXECUTE = 4u;
};
```
In addition, FIDL generates the following methods for `FileMode`:

* Bitwise operators: implementations for the `|`, `|=`, `&`, `&=`, `^`, `^=`,
  and `~` operators will be generated, allowing bitwise operations on the bits
  like: `mode |= FileMode::EXECUTE`.

FIDL also generates a `const static FileMode FileModeMask` variable.
This is a bitmask containing all of the bits in the enum class, which can be
used to get rid of any unused bit values from a raw underlying `uint16_t`
(or whichever type the `bits` are based on). In the above example, `FileModeMask`
has a value of `0b111`.

### Enums {#enums}

Given an enum definition like:

```fidl
enum Color {
    RED = 1;
    GREEN = 2;
    BLUE = 3;
};
```

The FIDL toolchain generates an equivalent C++ `enum class` using the specified
underlying type, or `uint32_t` if none is specified:

```c++
enum class Color : uint32_t {
    RED = 1u;
    GREEN = 2u;
    BLUE = 3u;
};
```

### Structs {#structs}

Given a struct declaration:

struct Color {
    uint32 id;
    string name = "red";
};

The FIDL toolchain generates a `final` `WebPage` with `public`
members and methods.

* `public` members:
  * `uint32_t id{};`: since `id` has no default value, it is zero-initialized.
  * `std::string url = "www.example.com"`: the corresponding field for `url`.

* Methods:
  * `static inline std::unique_ptr<WebPage> New()`: returns a unique_ptr to a
    new `WebPage`.

The 6 special members of `WebPage` (default, copy and move constructor,
destructor, copy and move assignment) are implicitly defined.

`WebPage` also has the following associated generated values:

* `WebPagePtr`: an alias to `unique_ptr<WebPage>`.

Structs may have additional members if they represent the response variant of
a [result](#protocols-results).

### Unions {#unions}

The following terminology is used when discussing unions:

* Variant: the selected member of a union.
* Tag: the target language variant discriminator. In HLCPP, these are represented
  using `enum`s.
* Ordinal: the on the wire variant discriminator.

Given the following union definition:

```fidl
union JsonValue {
    1: reserved;
    2: int32 int_value;
    3: string string_value;
};
```

FIDL will generate a `final` `JsonValue` class. `JsonValue` contains a
public tag enum representing the possible variants:

```c++
enum Tag : fidl_xunion_tag_t {
  kIntValue = 2,
  kStringValue = 3,
  Invalid = std::numeric_limits<fidl_xunion_tag_t>::max(),
};
```

Each member of `Tag` has a value matching its ordinal specified in the `union`
definition. Reserved fields do not have any generated code. In addition, there
is an `Invalid` field which is the initial value of the tag of an instance of
the `JsonValue` class that has no variant set yet.

The FIDL toolchain generates the following methods on `JsonValue`:

* `JsonValue()`: Default constructor. The tag is initially `Tag::Invalid` until
  the `JsonValue` is set to a specific variant
* `~JsonValue()`: Default destructor
* `static JsonValue WithIntValue(int32&&)` and
  `static JsonValue WithStringValue(std::string&&)`: These static methods are
  used to construct a `Foo` directly from one of the variants.
* `static inline std::unique_ptr<JsonValue> New()`: Returns a `unique_ptr` to
  a new `JsonValue`
* `bool has_invalid_tag()`: Returns `true` if the instance of `JsonValue` does
   not yet have a variant set. Users should not access a union until a variant
   is set - doing so should be considered undefined behavior.
* `bool is_int_value() const` and `bool is_string_value() const`: Each variant
  has an associated method to check whether an instance of `JsonValue` is of
  that variant
* `const int32_t& int_value() const` and
  `const std::string string_value() const`: Read-only accessor methods for each
  variant. These methods will fail if `JsonValue` does not have the specified
  variant set
* `int32_t& int_value()` and `std::string string_value()`: Mutable accessor
  methods for each variant. If the `JsonValue` has a different variant than
  the called accessor method, it will destroy its current data and
  re-initialize it as the specified variant.
* `JsonValue& set_int_value(class int32_t value)` and
  `Foo& set_string_value(class std::string value)`: Setter methods for
  each variant.
* `Tag Which() const`: returns the current tag of the `Foo`.
* `fidl_xunion_tag_t Ordinal() const`: returns the raw `fidl_xunion_tag_t` tag.
  Prefer to use `Which()` unless the raw ordinal is required

`JsonValue` also has the following associated generated values:

* `JsonValuePtr`: an alias to `unique_ptr<Foo>`.

Unions may have additional methods if they represent the response
variant of a [result](#protocols-results).

#### Flexible unions and unknown variants

Flexible unions (that is, unions that are prefixed with the `flexible` keyword
in their FIDL definition) will have an extra variant in the generated `Tag`:

```c++
enum Tag : fidl_xunion_tag_t {
    kUnknown = 0,
    ... // other fields omitted
};
```

When a FIDL message containing a union with an unknown variant is decoded into
`JsonValue`, `JsonValue::Which()` will return `JsonValue::Tag::kUnknown`, and
`JsonValue::Ordinal()` will return the unknown ordinal.

`JsonValue` will also have the following extra methods:

* `const vector<uint8_t>* UnknownData() const`: returns the raw bytes of the
  union variant

HLCPP does not support encoding a `JsonValue` if it has an unknown ordinal.
Re-encoding a union with an unknown variant does not write the unknown bytes
back onto the wire, instead encoding an absent union. Therefore, encoding a
message with an uninitialized union will only succeed if this union is allowed
to be absent (i.e. if it is specified as `JsonValue?` instead of `JsonValue`).

Non-flexible (i.e. `strict`) unions fail when decoding a data containing an
unknown variant.

### Tables {#tables}

Given the following table definition:

```table
table User {
    1: reserved;
    2: uint8 age;
    3: string name;
};
```

The FIDL toolchain will generate a `final` `User` class that defines the
following methods:

* `User()`: Default constructor, initializes with all fields unset.
* `User(User&& other)`: Move constructor.
* `~User()`: Destructor.
* `User& User::operator=(User&& other)`: Move assignment.
* `bool IsEmpty() const`: Returns whether no fields are set.
* `bool has_x() const` and `bool has_y() const`: Returns whether a field
  is set.
* `const uint8_t& age() const` and `const std::string& name() const`:
  Read-only field accessor methods. These fail if the field is not set.
* `uint8_t* mutable_age()` and `std::string* mutable_age()`: Mutable field
  accessor methods. If the field is not set, a default one will be constructed,
  set, and returned.
* `User& set_age(uint8_t _value)` and `User& set_name(std::string _value)`:
  Field setters.
* `void clear_age()` and `void clear_name()`: Clear the value of a field by
  calling its destructor

`User` also has the following associated generated values:
* `UserPtr`: an alias to `unique_ptr<User>`.

## Protocols {#protocols}

Given a protocol:

```fidl
protocol TicTacToe {
    StartGame(bool start_first);
    MakeMove(uint8 row, uint8 col) -> (bool success, GameState? new_state);
    -> OnOpponentMove(GameState new_state);
};
```

FIDL will generate a `TicTacToe` class, which acts as an entry point for
interacting with the protocol and defines the interface of the service which
is used by clients to proxy calls to the server, and for the server for
implementing the protocol.

`TicTacToe` contains the following member types:

* `MakeMoveCallback` and `OnOpponentMoveCallback`: Each response and event
  has a member type generated that represents the type of the callback for
  handling that response or event. In the above example, `MakeMoveCallback`
  aliases `fit::function<void(bool, std::unique_ptr<GameState>)>` and
  `OnOpponentMoveCallback` aliases `fit::function<void(GameState)>`.

`TicTacToe` additionally has the following pure virtual methods,
corresponding to the methods in the protocol definition:

* `virtual void StartGame(bool start_first) = 0`: Pure virtual method
  for a fire and forget protocol method. It takes as arguments the
  request parameters.
* `virtual void MakeMove(uint8_t row, uint8_t col, MakeMoveCallback callback) = 0`:
  Pure virtual method for a two way protocol method. It takes as arguments
  the request parameters followed by the response handler callback.

Other code may be generated depending on the
[attributes](#protocol-and-method-attributes) applied to the
protocol or its methods.

### Client

Clients do not use the `TicTacToe` class directly. Instead, the FIDL
toolchain generates two aliases for the classes used to make calls to
a server implementing the `TicTacToe` protocol: `TicTacToePtr`, which
aliases `fidl::InterfacePtr<TicTacToeProtocol>` representing an async
client, and `TicTacToeSyncPtr`, which aliases
`fidl::SynchronousInterfacePtr<TicTacToeProtocol>` representing a synchronous client.

When dereferenced, `TicTacToePtr` and `TicTacToeSyncPtr` return a proxy
class that implements `TicTacToe`, which is how the client makes requests to
the server. In this example, given a `TicTacToePtr` called `async_tictactoe`,
requests could be made by calling `async_tictactoe->StartGame(start_first)` or
`async_tictactoe->MakeMove(row, col, callback)`.

Examples on how to set up and bind an `InterfacePtr` or a
`SynchronousInterfacePtr` to a channel are covered in the HLCPP tutorial.

### Server

Implementing a server for a FIDL protocol involves providing a concrete
implementation of `TicTacToeProtocol`.

Examples on how to set up and bind a server implementation are covered in the
HLCPP tutorial.

### Events {#events}

#### Client

For a `TicTacToePtr` `tictactoe`, `tictactoe.events()` will return a proxy
class that contains the following public members:

* `OnOpponentMoveCallback OnOpponentMove`: The callback handler for the
  `OnOpponentMove` event.

Clients can handle events by setting the members of this class to the desired
event handlers.

#### Server

For a `Binding<TicTacToe>` `tictactoe`, `tictactoe.events()` will return
a stub class that contains the following public members:

* `void OnOpponentMove(GameState new_state)`: Send an `OnOpponentMove`.

### Results {#protocols-results}

Given a method:

```fidl
protocol TicTacToe {
    MakeMove(uint8 row, uint8 col) -> (GameState new_state) error MoveError;
};
```

FIDL generates code so that clients and servers can use `fit::result` in
place of the generated `MakeMove` response type. This is done by generating
a `TicTacToe_MakeMove_Result` (following the naming scheme
`[protocol]_[method]_Result`) class to represent the response, which is
interchangeable with `fit::result<GameState, MoveError>`. Using this feature,
an example implementation of `MakeMove` on the server side could look like:

```c++
void  MakeMove(MakeMoveCallback callback) override {
    callback(fit::ok(game_state_.state()));
    // or, in the error case: callback(fit::error(Error::kInvalid);
}
```

An example of using this on the client side, in the async case would be:

```c++
async_game->MakeMove([&](fit::result<GameState, MoveError>> response) { ... });
```

When generating code, the FIDL toolchain treats `TicTacToe_MakeMove_Result`
as a `union` with two variants: `response`, which is a generated type described
below, and `err`, which is the error type (in this case `uint32`), which means
that it provides all the methods available to a
[regular union](#unions). In addition,
`TicTacToe_MakeMove_Result` provides methods that allow interop with `fit::result`:

* `TicTacToe_MakeMove_Resultfit::result<GameState, MoveError>&& result)`: Move
  constructor from a `fit::result`.
* `TicTacToe_MakeMove_Result(fit::ok_result<GameState>&& result)`: Move constructor
  from a `fit::ok_result`.
* `TicTacToe_MakeMove_Result(fit::error_result<MoveError>&& result)`: Move
  constructor from a `fit::error_result`.
* `operator fit::result<GameState, MoveError>() &&`: Conversion to a
  `fit::result`.

Note that the successful result type parameter of the `fit::result` follows the
[parameter type conversion rules](#protocol-and-method-attributes): if `MakeMove`
returned multiple values on success, the result type would be a tuple of the
response parameters `fit::result<std::tuple<...>, ...>`, and if `MakeMove`
returned an empty response, the result type would be `fit::result<void, ...>`.

The FIDL toolchain also generates a `TicTacToe_MakeMove_Response` class, which
is the type of the `response` variant of `TicTacToe_MakeMove_Result`. This class
is treated as a FIDL struct with fields corresponding to each parameter of the
successful response. In addition to the methods and members available to a
[regular struct](#structs), `TicTacToe_MakeMove_Response` provides additional
methods that allow interop with `std::tuple`:

* `explicit TicTacToe_MakeMove_Response(std::tuple<GameState> _value_tuple)`:
  Constructor from a tuple.
* `operator std::tuple<GameState>() &&`: Conversion operator for a tuple.

### Protocol composition {#protocol-composition}

FIDL does not have a concept of inheritance, and generates full code as
described above for all composed protocols. In other words, the code
generated for

```fidl
protocol A {
    Foo();
};

protocol B {
    compose A;
    Bar();
};
```

Provides the same API as the code generated for:

```fidl
protocol A {
    Foo();
};

protocol B {
    Foo();
    Bar();
};
```

The generated code is identical except for the method ordinals.

### Protocol and method attributes {#protocol-and-method-attributes}

#### Transitional

For protocols annotated with the
[`[Transitional]`](/docs/reference/fidl/language/attributes.md#transitional)
attribute, the `virtual` methods on the protocol class are not pure. This allows
implementations of the protocol class with missing method overrides to compile successfully.

#### Discoverable

A protocol annotated with the
[`[Discoverable]`](/docs/reference/fidl/language/attributes.md#discoverable)
attribute causes the FIDL toolchain to generate an additional
`static const char Name_[]` field on the protocol class, containing the full
protocol name. For a protocol `Baz` in the library `foo.bar`, the generated
name is `"foo.bar.Baz"`.

### Test scaffolding {#test-scaffolding}

The FIDL toolchain also generates a file suffixed with  `_test_base.h` that
contains convenience code for testing FIDL server implementations. This file
contains a class for each protocol that provides stub implementations for each
of the class’s methods, making it possible to implement only the methods that
are used during testing. These classes are generated into a `testing` namespace
that is inside of the generated library’s namespace (e.g. for library
`games.tictactoe`, these classes are generated into `games::tictactoe::testing`).

Given a `protocol`:

```fidl
protocol TicTacToe {
    StartGame(bool start_first);
    MakeMove(uint8 row, uint8 col) -> (bool success, GameState? new_state);
    -> OnOpponentMove(GameState new_state);
};
```

The FIDL toolchain generates a `TicTacToe_TestBase` class that subclasses
`TicTacToe` (see [Protocols](#protocols)), offering the following methods:

* `virtual ~TicTacToe_TestBase() {}`: Destructor.
* `virtual void NotImplemented_(const std::string& name) = 0`: Pure virtual
  method that is overridden to define behavior for unimplemented methods.

`TicTacToe_TestBase` provides an implementation for the virtual protocol
methods `StartGame` and `MakeMove`, which are implemented to just call
`NotImplemented_("StartGame")` and `NotImplemented_("MakeMove")`, respectively.
