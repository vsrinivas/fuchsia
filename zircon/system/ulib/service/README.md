# LLCPP FIDL Service library

This library provides low-level C++ (LLCPP) APIs to connect to and serve FIDL Services.

A FIDL Service is a logical grouping of FIDL protocols, exposed as members of a virtual
directory.

There can exist multiple instances of a FIDL Service in a component's incoming namespace.

For example, a component `use`-ing a FIDL Service might have an incoming namespace that
looks like:
```
/svc
    /fidl.examples.Echo
                       /default
                               /foo
                               /bar
                       /other
                             /foo
                             /bar
    /...
```

FIDL Services were introduced in [FTP-041].

## Example FIDL Service declaration

```fidl
library fidl.examples.echo;

service EchoService {
    EchoProtocol foo;
    EchoProtocol bar;
};

protocol EchoProtocol {
    // ...
}
```

## Client API

The client API allows callers to enumerate the instances of a FIDL Service, connect to
an instance, enumerate its members, and establish connections to those member protocols,
all in a type-safe API.

A FIDL Service's name and members are derived from its generated LLCPP FIDL bindings.
The service library uses that information to generate convenient type-safe APIs that
still allow the caller to manage memory allocations on their own.

Any buffers that are required by the library are small and allocated on the stack.

To get started, see the documentation for `sys::OpenServiceAt<FidlService>`, in
`<lib/service/llcpp/service.h>`.

[FTP-041]: https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/ftp/ftp-041.md