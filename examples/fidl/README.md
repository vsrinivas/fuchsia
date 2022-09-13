 w# FIDL examples

This directory contains example code for using FIDL. `fuchsia.examples` contains
FIDL definitions used in the various bindings-specific example directories for
hlcpp, cpp, go, and rust.

The `test` directory contains tests to ensure that example code is kept up to
date. To run tests:

```posix-terminal
fx set core.x64 --with //examples/fidl:tests
fx test -vo //examples/fidl
```

Included in this are integration tests which will launch and run client/server
examples against each other and check for successful exist codes. To run the
integration tests:

```posix-terminal
fx test -vo //examples/fidl/echo-realm
```

The tests include many individual packages, so you can filter for specific
client/server pairs by specifying a particular package in
`//examples/fidl/echo-realm/BUILD.gn`:

```posix-terminal
fx test -vo echo-cpp-client-test
```

All major FIDL code examples used in documentation should be included from a
source file somewhere in this documentation that is covered by tests. This
eliminates sample code duplication and ensures that example code is kept up to
date and correct.

Although this ensures that code used in any documentation is kept up to date, it
does not ensure that text referring to the code is kept in sync. When modifying
code in this repo, please check that any corresponding text is kept in sync.
Code can be included using a specific region:

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/foo" region_tag="bar" %}
```

or using an entire file

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/foo" %}
```

References can be found by grepping for these two variants in `/docs`.
