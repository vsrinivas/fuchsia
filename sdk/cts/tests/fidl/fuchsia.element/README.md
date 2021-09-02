# `fuchsia.element` CTS test

This is a compatibility test for `fuchsia.element` FIDL APIs.

## Motivation

The [Session Framework][doc-sfw-intro] provides tools and APIs to create product
experiences on Fuchsia that combine software from the platform (e.g.
`session_manager`), product (the [session][glossary-session]), and third-parties
([elements][glossary-element]).  Interoperability between components is
a challenge because they may implement the framework’s contracts differently or
rely on undocumented behaviors.

The Fuchsia [Compatibility Test Suite][doc-cts-rfc] (CTS) is a collection of
tests that ensure a Fuchsia product build is backwards-compatible with software
written against previous versions of the platform.

Session compatibility tests are a subset of the CTS that exercise contracts
between:

1. **Platform and session**: ensuring platform changes do not break existing
   sessions, including changes to platform-provided session components (e.g.
   input pipeline, `element_manager`)
2. **Session and elements**: ensuring sessions are compatible with common types
   of elements — for example, that a Flutter app is launched and presented
   similarly on different products.

## Component Topology

Session CTS tests are CFv2 components that launch a Session Framework and the
session under test in an isolated environment.

The test launches [`session_manager`][glossary-session-manager], which in turn
launches the session configured for the current product (with the
[`session_config`][source-session-config] GN rule). This allows the same test to
run against any session without modifying the test, simply by supplying a new
configuration.

Session CTS includes a reference session-under-test that is composed from
platform-provided components. It is intended for testing the Session CTS itself
(all tests should pass with the reference session), and for integration testing
the platform-provided components.

[`element_manager`][source-element-manager] is a platform-provided component
that serves the `fuchsia.element.Manager` protocol by wrapping the Rust
[`element_management`][source-element-management] library.

![Diagram of component topology with CTS-provided reference
session](images/session-cts-topology-1.png)

The product may be configured with a different session-under-test. In this case,
`session_manager` launches that session instead of the reference session:

![Diagram of component topology with product-provided
session](images/session-cts-topology-2.png)

## Reference session

Tests that exercise launching elements may use a reference element provided by
the CTS.

The test suite ships with a reference session implementation that passes all
tests. It is a minimal session that contains just enough to exercise the tests,
and is not intended to be a fully-fledged example of a product.

The reference session is not tailored specifically for the test environment.
Neither the tests nor the reference session know about each other’s existence,
as to avoid coupling tests to a particular session implementation.

The reference session reuses platform-provided components as much as possible.
This encourages code reuse between products and frees developers from worrying
about implementing boilerplate functionality (e.g. launching element
components). This code needs to be packaged as a component, not a library,
because the CTS may only depend on SDK atoms.

## Tests

### `element_manager_test`: Proposing an element is successful

The simplest possible test for the
[fuchsia.element.Manager][source-element-manager-fidl] protocol proposes an
element and asserts that the result was successful.

## Example usage

```
$ fx set core.x64 --with-base "//sdk/cts"
$ fx test fuchsia-element-tests -o
```

### Configuring session under test

TODO(fxbug.dev/83905): The tests will currently always run the reference-session.
The instructions below are valid once this test can run the `session_manager`'s
configured session.

The tests require that the build contains a `session_manager` configuration,
typically by including a `session_config` target in the base image.
The CTS tests will use the session specified in this configuration as
the session under test.

The `fuchsia.element` CTS tests includes a reference session used to validate
the tests, `reference-session`:

```
fx set ... --with "//sdk/cts/tests/fidl/fuchsia.element:reference-session-config"
```

Other sessions can be tested by using a different `session_config`.

Note that the `session_config` rule configures the system to start the session
on boot. The CTS tests do not use this instance of the session in any way.
Instead, each test launches a separate instance in an isolated environment,
used just for that test.

Some products, like workstation, already include a `session_config` in their
build, so you should not include it in `fx set`.

[doc-cts-rfc]: /docs/contribute/governance/rfcs/0015_cts.md
[doc-event-capabilities]: /docs/concepts/components/v2/capabilities/event.md
[doc-sfw-intro]: /docs/concepts/session/introduction.md
[glossary-element]: /docs/glossary.md#element
[glossary-session]: /docs/glossary.md#session
[glossary-session-manager]: /docs/glossary.md#session-manager
[source-element-management]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/session/lib/element_management
[source-element-manager-fidl]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.element/element_manager.fidl
[source-element-manager]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/session/bin/element_manager
[source-session-config]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/session/build/session_config.gni
