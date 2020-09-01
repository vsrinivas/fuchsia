# Introduction to the Fuchsia Testing Framework

This document introduces the Fuchsia Testing Framework (FTF) along with
fundamental concepts and terminology around testing in Fuchsia.

Note: The Fuchsia Testing Framework is only supported from
[Components v2][glossary-components-v2]. All component-related concepts
discussed herein refer to Components v2 unless otherwise noted.

## Tests as components {#tests-as-components}

Like any other Fuchsia program, a Fuchsia test is a
[component][glossary-component]. Like all components, a Fuchsia test component
may be viewed as a [realm][realms], or tree of components, also called a *test
realm*. The component at the root of the test realm is known as the *test root*.

For a component to be a test, it must [manifests-expose][manifests-expose] and
implement the [`fuchsia.test.Suite`](#test-suite-protocol) protocol.

Components in the test realm may play various roles, as defined in
[test roles](#test-roles).

<!-- TODO: add diagrams -->

## The test suite protocol {#test-suite-protocol}

The test suite protocol, [`fuchsia.test.Suite`][fidl-test-suite], is the
protocol implemented by a test that the test manager uses to run tests and
obtain information about them. Most commonly, the test code itself (i.e., the
[test driver](#test-roles)) is written using a language-specific test library,
and the protocol is implemented by a [test runner](#test-runner) on the test's
behalf.

## Test runners

The [fuchsia.test.Suite](#test-suite-protocol) protocol is the way for
[Components v2](#glossary-components-v2) tests to enumerate their test cases and
report results. This differs from [Components v1](#glossary-components-v1),
where test results are reported through stdout and exit codes. Most commonly, a
test will not implement the `fuchsia.test.Suite` protocol from scratch, but
would instead use a *test runner* that integrates a language-native testing
library with the protocol. The test component does this by declaring it uses the
appropriate test runner in the [test driver][test-roles]'s manifest. For
example, for a C++ gtest, the test driver's manifest would contain the
following:

```json
// test_driver.cml
{
    ...
    "use": [
        ...
        // Use the gtest runner
        {
            "runner": "gtest_runner",
        },
    ],
    "expose": [
        ...
        // Test driver must still expose fuchsia.test.Suite
        {
            "protocol": "/svc/fuchsia.test.Suite",
            "from": "self",
        },
    ],
}
```

However, it's cumbersome for a test to manually implement this protocol. For
this reason, the Fuchsia Testing Frameworks provides a set of special runners
called *test runners*. A test runner integrates a [test driver](#test-roles)
that uses a language-native test library (such as C++ `gtest` or rust `libtest`)
with the test suite protocol. If your component declares it uses the appropriate
test runner in its [component manifest][manifests], you can write your test
natively against the language-specific test library and do not need to manually
export results under the test suite protocol.

## Hermeticity

A test is *hermetic* if it [uses][manifests-use] or [offers][manifests-offer] no
capabilities from the [test root](#tests-as-components)'s parent. As a rule of
thumb, tests should be hermetic, but sometimes a test requires a capability that
cannot be injected in the test realm.

In the context of hermetic tests, a capability that originates from outside of
the test's realm is called a *system capability*.

## Test roles {#test-roles}

Components in the [test realm](#tests-as-components) may play various roles in
the test, as follows:

-   Test driver: The component that actually runs the test, and implements
    (either directly or through a [test runner](#test-runners)) the
    [`fuchsia.test.Suite`][test-suite-protocol] protocol. This role may be, but
    is not necessarily, owned by the [test root](#tests-as-components).
-   Capability provider: A component that provides a capability which the test
    will exercise somehow. The component may either provide a "fake"
    implementation of the capability for test, or a "real" implementation that
    is equivalent to what production uses.
-   Component under test: A component that exercises some behavior to be tested.
    This may be identical to a component from production, or a component written
    specifically for the test intended to model production behavior.

[fidl-test-suite]: /sdk/fidl/fuchsia.test/suite.fidl
[glossary-component]: ../../glossary.md#component
[glossary-components-v1]: ../../glossary.md#components-v1
[glossary-components-v2]: ../../glossary.md#components-v2
[manifests]: /docs/concepts/components/v2/component_manifests.md
[manifests-expose]: /docs/concepts/components/v2/component_manifests.md#expose
[manifests-offer]: /docs/concepts/components/v2/component_manifests.md#offer
[manifests-use]: /docs/concepts/components/v2/component_manifests.md#use
[realms]: /docs/concepts/components/v2/realms.md
[realms-definitions]: /docs/concepts/components/v2/realms.md#definitions
[test-suite-protocol]: /docs/concepts/components/v2/realms.md
