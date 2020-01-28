
# Comparing C, Low-Level C++, and High-Level C++ Language Bindings

[TOC]

## C Bindings

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
    a limited subset of FIDL messages
    ([[Layout = "Simple"]](
    /docs/development/languages/fidl/reference/attributes.md#layout) types).
*   Client is synchronous only. Two-way method calls will block.
*   As the Low-Level C++ Bindings mature, there are plans to re-implement
    the C bindings as a light-weight wrapper around the C++ bindings.

## Low-Level C++ Bindings

*   Optimized to meet the needs of low-level systems programming while providing
    slightly more safety and features than the C bindings.
*   Represent data structures whose memory layout coincides with the wire
    format, i.e. satisfying C++ Standard Layout. This opens the door to
    in-place encoding and decoding.
*   Support in-place access and construction of FIDL messages.
*   Generated structures are views of an underlying buffer; they do not own
    memory.
*   Use owned handle types such as `zx::handle`. Note that since generated
    structures are views of an underlying buffer, a parent structure will only
    own child handles if it also owns their underlying buffer. For example, a
    FIDL struct owns all the handles stored inline, but a FIDL vector of structs
    containing handles will be represented as a vector view, which will not own
    the out-of-line handles.
*   Defer all memory allocation decisions to the client.
*   Code generator produces only type declarations, coding tables, simple
    inline functions, and pure virtual server interfaces.
*   Client may manually dispatch incoming method calls on protocols
    (write their own switch statement and invoke argument decode functions).
*   Similar to the C language bindings but using zero-cost C++ features
    such as namespaces, string views, and array containers.
*   Client is synchronous only. However, async client support is planned.

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
*   Code generator produces more code compared to the low-level C++ bindings,
    and much more than the C bindings. This includes constructors, destructors,
    protocol proxies, protocol stubs, copy/move functions, and
    conversions to/from in-place buffers.
*   Client performs protocol dispatch by subclassing a provided stub and
    implementing the virtual methods for each operation.
*   Both async and synchronous clients are supported. However, the async clients
    are not thread-safe.

## Summary

Category                           | Simple C                          | Low-level C++                                 | High-level C++
-----------------------------------|-----------------------------------|-----------------------------------------------|--------------------
**audience**                       | drivers                           | drivers and performance-critical applications | high-level services
**abstraction overhead**           | almost zero                       | almost zero                                   | heap allocation, construction, destruction
**type safe types**                | enums, structs, unions            | enums, structs, unions, handles, protocols    | enums, structs, unions, handles, protocols
**storage**                        | stack                             | stack, in-place buffer, or heap               | heap
**lifecycle**                      | manual free (POD)                 | manual free memory; own handles via RAII [1]  | automatic free (RAII)
**receive behavior**               | copy                              | copy or decode in-place                       | decode then move to heap
**send behavior**                  | copy                              | copy or encode in-place                       | move to buffer then encode
**calling protocol methods**       | free functions                    | free functions or proxy                       | call through proxies, register callbacks
**implementing protocol methods**  | manual dispatch or via ops table  | manual dispatch or implement stub interface   | implement stub object, invoke callbacks
**async client**                   | no                                | no (planned)                                  | yes
**async server**                   | limited [2]                       | yes (unbounded) [3]                           | yes (unbounded)
**parallel server dispatch**       | no                                | yes [4]                                       | no
**generated code footprint**       | small                             | moderate                                      | large

Notes:

1. Generated types own all handles stored inline. Out-of-line handles e.g. those
   behind a pointer indirection are not closed when the containing object of the
   pointer goes away. In thoses cases, the bindings provide a
   `fidl::DecodedMessage` object to manage all handles associated with a call.
2. The bindings library can dispatch at most one in-flight transaction.
3. The bindings library defined in [lib/fidl-async](/zircon/system/ulib/fidl-async) can dispatch an unbounded number of in-flight transactions via `fidl::AsyncBind` defined in [lib/fidl-async/cpp/async_bind.h](/zircon/system/ulib/fidl-async/include/lib/fidl-async/cpp/async_bind.h).
4. The bindings library [lib/fidl-async](/zircon/system/ulib/fidl-async) enables
parallel dispatch using the `EnableNextDispatch()` API defined in
[lib/fidl-async/cpp/async_transaction.h](/zircon/system/ulib/fidl-async/include/lib/fidl-async/cpp/async_transaction.h).

## Migrating From C Bindings To Low-Level C++

TODO
