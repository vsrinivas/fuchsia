# The Fuchsia Test Runner Framework

<<../components/_v2_banner.md>>

## Integrating testing frameworks with the Component Framework

The Fuchsia [Component Framework][cf] allows developers to create components in
a variety of languages and runtimes. Fuchsia's own code uses a diverse mix of
programming languages for components, including C/C++, Rust, Dart, and Go.

The Test Runner Framework uses the Component Framework's concept of
[runners][runners] as an integration layer between various languages & testing
runtimes and a common Fuchsia protocol for launching tests and receiving their
results.

## The test suite protocol {#test-suite-protocol}

The test suite protocol, [`fuchsia.test.Suite`][fidl-test-suite], is the
protocol implemented by a test that the test manager uses to run tests and
obtain information about them. A component that implements this protocol may be
launched as a test (such as with `fx test`). Using this protocol, the test
component is responsible for receiving requests to run (for instance which test
cases to run and how many test cases to run in parallel) and reporting their
results (for instance which test cases were run and whether they passed or
failed).

Test authors typically don't need to implement this protocol. Instead, they rely
on a [test runner](#test-runners) to do this for them. For instance, you might
write a test in C++ using the GoogleTest framework, and then use
[`gtest_runner`](#gtest-runner) in your [component manifest][component-manifest]
to integrate with the Test Runner Framework.

## Test runners {#test-runners}

Test runners are reusable adapters between the Test Runner Framework and common
languages & frameworks used by developers to write tests. They implement the
[`fuchsia.test.Suite`][fidl-test-suite] protocol on behalf of the test author,
leaving the test author to focus on their test logic.

Component manifests for simple unit tests can be [generated][unit-tests]
by the build rules. Generated component manifests for v2 tests will include the
appropriate test runner based on their build definition. For instance a test
executable that depends on the GoogleTest library will include the
[GoogleTest runner](#gtest-runner) in its generated manifest.

### Inventory of test runners

The following test runners are currently available for general use:

#### GoogleTest runner {#gtest-runner}

A runner for tests written in C/C++ using the GoogleTest framework.
Use this for all tests written using GoogleTest.

Common GoogleTest features are supported, such as disabling tests, running
only specified tests, running the same test multiple times, etc'.
Standard output, standard error, and logs are captured from the test.

In order to use this runner, add the following to your component manifest:

```json5
{
    include: [ "src/sys/test_runners/gtest/default.shard.cml" ]
}
```

By default GoogleTest test cases run serially (one test case at a time).

#### Rust runner {#rust-runner}

A runner for tests written in the Rust programming language and following Rust
testing idioms.
Use this for all idiomatic Rust tests (i.e. tests with modules that set the
attribute `[cfg(test)]`).

Common Rust testing features are supported, such as disabling tests, running
only specified tests, running the same test multiple times, etc'.
Standard output, standard error, and logs are captured from the test.

In order to use this runner, add the following to your component manifest:

```json5
{
    include: [ "src/sys/test_runners/rust/default.shard.cml" ]
}
```

By default Rust test cases run in parallel, at most 10 cases at a time.

#### Go test runner {#gotest-runner}

A runner for tests written in the Go programming language and following Go
testing idioms.
Use this for all tests written in Go using `import "testing"`.

Common Go testing features are supported, such as disabling tests, running
only specified tests, running the same test multiple times, etc'.
Standard output, standard error, and logs are captured from the test.

In order to use this runner, add the following to your component manifest:

```json5
{
    include: [ "src/sys/test_runners/gotests/default.shard.cml" ]
}
```

By default Go test cases run in parallel, at most 10 cases at a time.

#### ELF test runner {#elf-test-runner}

The simplest test runner - it waits for your program to terminate, then reports
that the test passed if the program returned zero or that it failed for any
non-zero return value.

Use this test runner if your test is implemented as an ELF program (for instance
an executable written in C/C++) but it does not use a common testing framework
that's supported by existing runners and you'd rather not implement a bespoke
test runner.

In order to use this runner, add the following to your component manifest:

```json5
{
    include: [ "src/sys/test_runners/elf/default.shard.cml" ]
}
```

There is no notion of parallelism since tests that use this runner don't have a
notion of multiple test cases.

### Controlling parallel execution of test cases

When using `fx test` to launch tests, they may run each test case in sequence or
run multiple test cases in parallel up to a given limit. The default parallelism
behavior is determined by the test runner.

To manually set the parallelism level for test cases, run the following:

```posix-terminal
fx shell run-test-suite --parallel=5 <test_url>
```

## Temporary storage

To use temporary storage in your test, add the following to your component manifest:

```json5
{
    include: [ "src/sys/test_runners/tmp_storage.shard.cml" ]
}
```

At runtime, your test will have read/write access to `/tmp`.
The contents of this directory will be empty when the test starts, and will be
deleted after the test finishes.

## Hermeticity

A test is *hermetic* if it [uses][manifests-use] or [offers][manifests-offer] no
capabilities from the [test root](#tests-as-components)'s parent. As a rule of
thumb, tests should be hermetic, but sometimes a test requires a capability that
cannot be injected in the test realm.

In the context of hermetic tests, a capability that originates from outside of
the test's realm is called a *system capability*.

## Test roles {#test-roles}

Components in the test realm may play various roles in the test, as follows:

-   Test driver: The component that actually runs the test, and implements
    (either directly or through a [test runner](#test-runners)) the
    [`fuchsia.test.Suite`][test-suite-protocol] protocol. This role may be, but
    is not necessarily, owned by the [test root](#tests-as-components).
-   Capability provider: A component that provides a capability that the test
    will exercise somehow. The component may either provide a "fake"
    implementation of the capability for test, or a "real" implementation that
    is equivalent to what production uses.
-   Component under test: A component that exercises some behavior to be tested.
    This may be identical to a component from production, or a component written
    specifically for the test intended to model production behavior.

[cf]: /docs/concepts/components/v2/
[component-manifest]: /docs/concepts/components/v2/component_manifests.md
[fidl-test-suite]: /sdk/fidl/fuchsia.test/suite.fidl
[manifests-offer]: /docs/concepts/components/v2/component_manifests.md#offer
[manifests-use]: /docs/concepts/components/v2/component_manifests.md#use
[runners]: /docs/concepts/components/v2/capabilities/runners.md
[test-suite-protocol]: /docs/concepts/components/v2/realms.md
