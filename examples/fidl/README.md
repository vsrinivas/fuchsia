# FIDL Examples

This directory contains example code for using FIDL.
`fuchsia.examples` contains FIDL definitions used in the various
bindings-specific example directories for hlcpp, llcpp, go, and rust.

The `test` directory contains tests to ensure that example code is kept
up to date. To run tests:

    fx set core.x64 --with //examples/fidl:tests
    fx test -vo //examples/fidl

Included in this are integration tests which will launch and run client/server
examples against eachother and check for successful exist codes. To run just the
integration test:

    fx test -vo examples-integration-test

This test is written using gtest, so you can filter for specific
client/server pairs as you would any other gtest:

    fx test -vo examples-integration-test -- '--gtest_filter=IntegrationTest.Llcpp*'

All major FIDL code examples used in documentation should preferably be
included from a source file somewhere in this documentation that is covered
by tests. This eliminates sample code duplication and ensures that example
code is kept up to date by being tested.

Although this ensures that code used in any documentation is kept up to date,
it does not ensure that text referring to the code is kept in sync. When
modifying code in this repo, please check that any corresponding text is kept
in sync. Code can be included using a specific region:

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/foo" region_tag="bar" %}
```

or using an entire file

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/foo" %}
```

References can be found by grepping for these two variants in `/docs`.
