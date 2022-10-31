# `fuchsia.element` CTS test

This is a compatibility test for `fuchsia.element` FIDL APIs.

## Motivation

Fuchsia provides tools and APIs to create product experiences that combine
software from the platform (e.g. `session_manager`), product (the [session
component][glossary.session-component]), and user-visible components.
Interoperability between components is a challenge because they may implement
the framework’s contracts differently or rely on undocumented behaviors.

The Fuchsia [Compatibility Test Suite][doc-cts-rfc] (CTS) is a collection of
tests that ensure a Fuchsia product build is backwards-compatible with software
written against previous versions of the platform.

Session compatibility tests are a subset of the CTS that exercise contracts
between:

1. **Platform and session**: ensuring platform changes do not break existing
   sessions, including changes to platform-provided session components (e.g.
   input pipeline, `element_manager`)
2. **Session and user-visible components**: ensuring sessions are compatible
   with common types of user-visible components — for example, that a Flutter
   app is launched and presented similarly on different products.

## Component Topology

Session CTS tests are CFv2 components that launch a Session Framework and the
session under test in an isolated environment.

The tests start a reference session that is composed from platform-provided
components. It is intended for testing the Session CTS itself (all tests should
pass with the reference session), and for integration testing the
platform-provided components.

![Diagram of the fuchsia-element-test component
topology](images/fuchsia-element-test-topology.png)

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
$ fx set core.x64 --with-base "//sdk/ctf"
$ fx test fuchsia-element-tests -o
```

[doc-cts-rfc]: /docs/contribute/governance/rfcs/0015_cts.md
[doc-event-capabilities]: /docs/concepts/components/v2/capabilities/event.md
[glossary.session-component]: /docs/glossary#session-component
[source-element-management]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/session/lib/element_management
[source-element-manager-fidl]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.element/element_manager.fidl
[source-element-manager]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/session/bin/element_manager
