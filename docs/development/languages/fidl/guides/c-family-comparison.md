# Comparing C, new C++, and high-level C++ language bindings

[TOC]

## [DEPRECATED] C bindings

The C bindings are deprecated in favor of [New C++ bindings](#cpp).

*   Optimized to meet the needs of low-level systems programming, plus tight
    constraints around dependencies and toolchains. The compiler, bindings
    library, and code-generator are written in C++, while exposing a pure C
    interface to clients.
*   Represent data structures whose memory layout coincides with the wire
    format.
*   Support in-place access and construction of FIDL messages.
*   Generated structures are views of an underlying buffer; they do not own
    memory.
*   Provide convenience wrappers for message construction and calling for
    a limited subset of FIDL messages (see
    [@for_deprecated_c_bindings][layout-attribute]).
*   Client is synchronous only. Two-way method calls will block.
*   As the New C++ bindings mature, there are plans to re-implement
    the C bindings as a light-weight wrapper around the C++ bindings.

## New C++ bindings {#cpp}

The new C++ bindings supports both low-level and high-level use cases, by
offering two families of generated domain objects, and corresponding client and
server APIs that speak those types.

### Natural types

*   Optimized to meet the needs of high-level service programming.
*   Represent data structures using idiomatic C++ types such as `std::vector`,
    `std::optional`, and `std::string`.
*   Use smart pointers to manage heap allocated objects.
*   Use `zx::handle` to manage handle ownership.
*   Can convert data between their wire (e.g. `fidl::StringView`) and natural
    type representations (e.g. `std::string`).

### Wire types

*   Optimized to meet the needs of low-level systems programming while providing
    slightly more safety and features than the C bindings.
*   Represent data structures whose memory layout coincides with the wire
    format, i.e. satisfying C++ Standard Layout. This opens the door to
    in-place encoding and decoding.
*   Generated structures are views of an underlying buffer; they do not own
    memory.
*   Support in-place access of FIDL messages.
*   Provide fine-grained control over memory allocation.
*   Use owned handle types such as `zx::handle`. Note that since generated
    structures are views of an underlying buffer, a parent structure will only
    own child handles if it also owns their underlying buffer. For example, a
    FIDL struct owns all the handles stored inline, but a FIDL vector of structs
    containing handles will be represented as a vector view, which will not own
    the out-of-line handles.

### Client and server APIs

*   Code generator produces more code compared to the C bindings. This includes
    constructors, destructors, copy/move functions, conversions between domain
    object families, protocol client implementations, and pure virtual server
    interfaces.
*   Users implement a server by sub-classing a provided server interface and
    overriding the pure virtual methods for each operation.
*   Clients supporting sync and async calls, and sync and async event handling.
*   Requires C++17 or above.

Refer to the [New C++ tutorial][cpp-tutorial] to get started.

## High-Level C++ Bindings

*   Optimized to meet the needs of high-level service programming.
*   Represent data structures using idiomatic C++ types such as `std::vector`,
    `std::optional`, and `std::string`.
*   Use smart pointers to manage heap allocated objects.
*   Use `zx::handle` (libzx) to manage handle ownership.
*   Can convert data from in-place FIDL buffers to idiomatic heap allocated
    objects.
*   Can convert data from idiomatic heap allocated objects
    (e.g. `std::string`) to in-place buffers (e.g. as a `fidl::StringView`).
*   Code generator produces more code compared to the C bindings. This includes
    constructors, destructors, protocol proxies, protocol stubs, copy/move
    functions, and conversions to/from in-place buffers.
*   Client performs protocol dispatch by sub-classing a provided stub and
    implementing the virtual methods for each operation.
*   Both async and synchronous clients are supported. However, the async clients
    are not thread-safe.
*   Requires C++14 or above.

Refer to the [HLCPP tutorial][hlcpp-tutorial] to get started.

## Summary

Category                           | [DEPRECATED] C                    | New C++ with wire types                   | New C++ with natural types             | High-level C++
-----------------------------------|-----------------------------------|-----------------------------------------------|--------------------------------------------|--------------------
**audience**                       | drivers                           | drivers and performance-critical applications | high-level services                        | high-level services
**abstraction overhead**           | almost zero                       | RAII closing of handles [[1]](#footnote1)     | heap allocation, construction, destruction | heap allocation, construction, destruction
**type safe types**                | enums, structs, unions            | enums, structs, unions, handles, protocols    | enums, structs, unions, handles, protocols | enums, structs, unions, handles, protocols
**storage**                        | stack                             | stack, user-provided buffer, or heap          | heap                                       | heap
**lifecycle**                      | manual free (POD)                 | manual or automatic free                      | automatic free (RAII)                      | automatic free (RAII)
**receive behavior**               | copy                              | decode in-place                               | decode into heap                           | decode then move to heap
**send behavior**                  | copy                              | copy or vectorize                             | copy                                       | copy
**calling protocol methods**       | free functions                    | free functions or proxy                       | free functions or proxy                    | call through proxies, register callbacks
**implementing protocol methods**  | manual dispatch or via ops table  | manual dispatch or implement stub interface   | implement stub interface                   | implement stub object, invoke callbacks
**async client**                   | no                                | yes                                           | yes                                        | yes
**async server**                   | limited [[2]](#footnote2)         | yes (unbounded) [[3]](#footnote3)             | yes (unbounded) [[3]](#footnote3)          | yes (unbounded)
**parallel server dispatch**       | no                                | yes [[4]](#footnote4)                         | yes [[4]](#footnote4)                      | no
**generated code footprint**       | small                             | large                                         | large                                      | large

--------------------------------------------------------------------------------

##### Footnote1

Generated types own all handles stored inline. Out-of-line handles e.g. those
behind a pointer indirection are not closed when the containing object of the
pointer goes away. In those cases, the bindings provide a `fidl::unstable::DecodedMessage`
object to manage all handles associated with a call.

##### Footnote2

The bindings library can dispatch at most one in-flight transaction.

##### Footnote3

The bindings library defined in [lib/fidl](/sdk/lib/fidl/cpp/wire) can
dispatch an unbounded number of in-flight transactions via `fidl::BindServer`
defined in
[lib/fidl/cpp/wire/channel.h](/sdk/lib/fidl/cpp/wire/include/lib/fidl/cpp/wire/channel.h).

##### Footnote4

The bindings library [lib/fidl](/sdk/lib/fidl/cpp/wire) enables parallel
dispatch using the `EnableNextDispatch()` API defined in
[lib/fidl/cpp/wire/async_transaction.h](/sdk/lib/fidl/cpp/wire/include/lib/fidl/cpp/wire/async_transaction.h).

## Migrating from C bindings to new C++ bindings

TODO

<!-- xrefs -->
[layout-attribute]: /docs/reference/fidl/language/attributes.md#layout
[cpp-tutorial]: /docs/development/languages/fidl/tutorials/llcpp
[hlcpp-tutorial]: /docs/development/languages/fidl/tutorials/hlcpp
