
# High Level C++ Language Bindings

This document is a description of the Fuchsia Interface Definition Language
(FIDL) implementation for C++, including its libraries and code generator.

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

*   Support encoding and decoding FIDL messages with C++14.
*   Small, fast, efficient.
*   Depend only on a small subset of the standard library.
*   Minimize code expansion through table-driven encoding and decoding.
*   All code produced can be stripped out if unused.
*   Reuse encoders, decoders, and coding tables generated for C language bindings.

## Code Generator

### Mapping Declarations

#### Mapping FIDL Types to C++ Types

This is the mapping from FIDL types to C types which the code generator
produces.

FIDL                                        | High-Level C++
--------------------------------------------|----------------------------------
`bool`                                      | `bool`
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
`string`                                    | `std::string`
`string?`                                   | `fidl::StringPtr`
`vector<T>`                                 | `std::vector<T>`
`vector<T>?`                                | `fidl::VectorPtr<T>`
`array<T>:N`                                | `std::array<T, N>`
*protocol, protocol?* Protocol              | *class* ProtocolPtr
*request\<Protocol\>, request\<Protocol\>?* | `fidl::InterfaceRequest<Protocol>`
*struct* Struct                             | *class* Struct
*struct?* Struct                            | `std::unique_ptr<Struct>`
*table* Table                               | *class* Table
*union* Union                               | *class* Union
*union?* Union                              | `std::unique_ptr<Union>`
*xunion* Xunion                             | *class* Xunion
*xunion?* Xunion                            | `std::unique_ptr<Xunion>`
*enum* Foo                                  | *enum class Foo : data type*

#### Mapping FIDL Identifiers to C++ Identifiers

TODO: discuss reserved words, name mangling

#### Mapping FIDL Type Declarations to C++ Types

TODO: discuss generated namespaces, constants, enums, typedefs, encoding tables

## Bindings Library

### Dependencies

Depends only on Zircon system headers, libzx, and a portion of the C and C++
standard libraries.

Does not depend on libftl or libmtl.

### Code Style

To be discussed.

The bindings library could use Google C++ style to match FIDL v1.0 but
though this may ultimately be more confusing, especially given style choices in
Zircon so we may prefer to follow the C++ standard library style here as well.

### High-Level Types

TODO: adopt main ideas from FIDL 1.0

InterfacePtr<T> / interface_ptr<T>?

InterfaceRequest<T> / interface_req<T>?

async waiter

etc...

## Suggested API Improvements over FIDL v1

The FIDL v1 API for calling and implementing FIDL protocols has generally been
fairly effective so we would like to retain most of its structure in the
idiomatic FIDL v2 bindings. However, there are a few areas that could be
improved.

TODO: actually specify the intended API

### Handling Connection Errors

Handling connection errors systematically has been a cause of concern for
clients of FIDL v1 because method result callbacks and connection error
callbacks are implemented by different parts of the client program.

It would be desirable to consider an API which allows for localized handling of
connection errors at the point of method calls (in addition to protocol level
connection error handling as before).

See https://fuchsia-review.googlesource.com/#/c/23457/ for one example of how
a client would otherwise work around the API deficiency.

One approach towards a better API may be constructed by taking advantage of the
fact that std::function<> based callbacks are always destroyed even if they are
not invoked (such as when a connection error occurs). It is possible to
implement a callback wrapper which distinguishes these cases and allows clients
to handle them more systematically. Unfortunately such an approach may not be
able to readily distinguish between a connection error vs. proxy destruction.

Alternately we could wire in support for multiple forms of callbacks or for
multiple callbacks.

Or we could change the API entirely in favor of a more explicit Promise-style
mechanism.

There are lots of options here...

TBD (please feel free to amend / expand on this)

[fidl-overview]: ../README.md
