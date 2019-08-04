# lib/fidl-service

A support library for FIDL service directories.

## Purpose

The purpose of this library is to provide a way to both consume or publish FIDL
service directories. That is, this library provides the building blocks for
developing clients or servers of FIDL service directories.

A FIDL service directory is a way to represent a FIDL service within the
namespace of a process. For example, take the following FIDL service:

```
library fuchsia.examples;

using fidl.examples.echo;

service MyService {
  fidl.examples.echo.Echo foo;
  fidl.examples.echo.Echo bar;
};
```

The FIDL service named MyService can be represented within the namespace of a
process with the following structure:

```
/svc/fuchsia.examples.MyService
/svc/fuchsia.examples.MyService/default
/svc/fuchsia.examples.MyService/default/foo
/svc/fuchsia.examples.MyService/default/bar
/svc/fuchsia.examples.MyService/alternate
/svc/fuchsia.examples.MyService/alternate/foo
/svc/fuchsia.examples.MyService/alternate/bar
```

Here we can see that `MyService` has two instances, `default` and `alternate`,
and each instance has the members `foo` and `bar`.

Using this library, we can interact we can interact with FIDL service
directories in code more simply. For example, here is how we would open the
`alternate` instance of `MyService` and bind to the member `foo`:

```
auto alternate = fidl::OpenService<fuchsia::examples::MyService>("alternate");
auto foo = alternate.foo().Connect().Bind();
```

For more examples, please see `//examples/fidl`.

## Dependencies

This library depends on:
* `fuchsia.io` - This is required to use `fuchsia::io::Directory`.
* `lib/async` - This is required to use `async::WaitMethod`.
* `lib/fdio` - This is required to use `fdio_ns_t`.
* `lib/fidl` - This is required to interact with FIDL.
* `lib/vfs` - This required to use `vfs::PseudoDir`.

Generated FIDL service code will depend on this library, and therefore inherit
the same set of dependencies.

Additionally, server processes may want to use `lib/sys` in order to simplify
their implementation.
