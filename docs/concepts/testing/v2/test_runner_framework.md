# The Fuchsia Test Runner Framework

<<../../components/_v2_banner.md>>

## Integrating testing frameworks with the Component Framework

The Fuchsia [Component Framework][cf] allows developers to create components in
a variety of languages and runtimes. Fuchsia's own code uses a diverse mix of
programming languages for components, including C/C++, Rust, Dart, and Go.

The Test Runner Framework uses Component Framework [runners][runners] as an
integration layer between various testing runtimes and a common Fuchsia protocol
for launching tests and receiving their results. This makes for an [inclusive]
design that on one hand allows developers to bring their language and testing
framework of choice, and on the other hand allows building and testing Fuchsia
on a variety of systems and targeting different hardware.

## The Test Manager

The `test_manager` component is responsible for running tests on a Fuchsia
device. Test manager exposes the
[`fuchsia.test.manager.RunBuilder`][fidl-test-manager] protocol, which allows
launching test suites.

Each test suite is launched as a child of test manager. Test suites are offered
capabilities by test manager that enable them to do their work while
maintaining isolation between the test and the rest of the system. For instance
hermetic tests are given the capability to log messages, but are not given the
capability to interact with real system resources outside of their sandbox.
Test manager uses only one capability from the test realm, a controller protocol
that test suites expose. This is done to ensure hermeticity (test results aren't
affected by anything outside of their intended sandbox) and isolation (tests
don't affect each other or the rest of the system).

The test manager controller itself is offered to other components in the system
in order to integrate test execution with various developer tools. Tests can
then be launched with such tools as [`fx test`][fx-test] and [`ffx`][ffx].

## The test suite protocol {#test-suite-protocol}

The test suite protocol, [`fuchsia.test.Suite`][fidl-test-suite], is used by the
test manager to control tests, such as to invoke test cases and to collect their
results.

Test authors typically don't need to implement this protocol. Instead, they rely
on a [test runner](#test-runners) to do this for them. For instance, you might
write a test in C++ using the GoogleTest framework, and then use
[`gtest_runner`](#gtest-runner) in your [component manifest][component-manifest]
to integrate with the Test Runner Framework.

## Test runners {#test-runners}

### A language and runtime-inclusive framework

Test runners are reusable adapters between the Test Runner Framework and common
languages & frameworks used by developers to write tests. They implement the
[`fuchsia.test.Suite`][fidl-test-suite] protocol on behalf of the test author,
allowing developers to write idiomatic tests for their language and framework of
choice.

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
    include: [ "//src/sys/test_runners/gtest/default.shard.cml" ]
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
    include: [ "//src/sys/test_runners/rust/default.shard.cml" ]
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
    include: [ "//src/sys/test_runners/gotests/default.shard.cml" ]
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
    include: [ "//src/sys/test_runners/elf/default.shard.cml" ]
}
```

If you are [using in-tree unit test GN templates][component-unit-tests],
and you are not already using a test framework with a dedicated test runner,
add the following to your build deps:

```
fuchsia_unittest_package("my-test-packkage") {
    // ...
    deps = [
        // ...
        "//src/sys/testing/elftest",
    ]
}
```

Note: If you see the error message "Component has a \`program\` block defined,
but doesn't specify a \`runner\`" for your test, this indicates you are not using a
test framework with a dedicated test runner, and you should add the above dependency.

#### Legacy test runner {#legacy-test-runner}

Legacy tests are tests that were written before the Test Runner Framework was
introduced. The legacy test runner offers a simple adapter between the modern
test framework and legacy tests that were not converted to modern ones. For help
with migrations see [this guide][sys-migration-guide].
**It is not recommended to use the legacy test runner in new tests.**

The legacy test runner detects if a test passed or failed by observing its
return code, with zero indicating success and non-zero indicating failure.

All legacy tests are automatically wrapped in a modern test and executed using
the legacy test runner. The launch URL of the wrapper will be derived from the wrapped
test's launch URL. For instance:

&nbsp;&nbsp;&nbsp;&nbsp;`fuchsia-pkg://fuchsia.com/package#meta/test_component.cmx`

will become:

&nbsp;&nbsp;&nbsp;&nbsp;`fuchsia-pkg://fuchsia.com/package#meta/test_component.cm`

The legacy test runner does not understand concepts such as test cases (or
filtering on them), running multiple test cases in parallel, etc. It does
however forward arguments to the test, so you can pass arguments that are
specific to the underlying test framework. For instance, to run just a specific
test case from a gtest:

```posix-terminal
fx test <test> -- --gtest_filter=MyTestCase
```

To run Rust tests, at most 5 test cases at a time:

```posix-terminal
fx test <test> -- --test-threads=5
```

To suppress this behavior set `wrap_cmx_test_with_cml_test` to false on `fuchsia_test_package`
or `fuchsia_unittest_package`. **Don't forget to file a bug and track the reason
for the exclusion.**

Change your `BUILD.gn` to exclude your legacy test:

```gn
import("//build/components.gni")

# This is your legacy test
fuchsia_test_component("simple_test_legacy") {
  component_name = "simple_test"
  manifest = "meta/simple_test.cmx"
  deps = [ ":simple_test_bin" ]
}

# Exclude your test from auto-wrapping.
fuchsia_test_package("simple_test") {
  test_components = [ ":simple_test_legacy" ]

  # TODO(fxbug.dev/XXXXX) : Excluding the test due to ...
  # Remove below line once the issue is fixed.
  wrap_cmx_test_with_cml_test = false
}

```

### Controlling parallel execution of test cases

When using `fx test` to launch tests, they may run each test case in sequence or
run multiple test cases in parallel up to a given limit. The default
parallelism behavior is determined by the test runner. To manually control the
number of test cases to run in parallel use test spec:

```gn
fuchsia_test_package("my-test-pkg") {
  test_components = [ ":my_test_component" ]
  test_specs = {
    # control the parallelism
    parallel = 10
  }
}
```

To override the value specified in the test spec, pass the parallel option when
invoking fx test:

```posix-terminal
fx test --parallel=5 <test_url>
```

### Running test multiple times

To run a test multiple times use:

```posix-terminal
 fx test --count=<n> <test_url>
```

If an iteration times out, no further iteration will be executed.

### Passing arguments

Custom arguments to the tests can be passed using `fx test`:

```posix-terminal
fx test <test_url> -- <custom_args>
```

Individual test runners have restrictions on these custom flags:

#### GoogleTest runner {#gtest-runner-custom-arg}

Note the following known behavior change:

**--gtest_break_on_failure**: As each test case is executed in a different process,
this flag will not work.

The following flags are restricted and the test fails if any are passed as
fuchsia.test.Suite provides equivalent functionality that replaces them.

- **--gtest_filter** - Instead use:

```posix-terminal
 fx test --test-filter=<glob_pattern> <test_url>
```

`--test-filter` may be specified multiple times. Tests that match any of the
given glob patterns will be executed.

- **--gtest_also_run_disabled_tests** - Instead use:

```posix-terminal
 fx test --also-run-disabled-tests <test_url>
```

- **--gtest_repeat** - See [Running test multiple times](#running_test_multiple_times).
- **--gtest_output** - Emitting gtest json output is not supported.
- **--gtest_list_tests** - Listing test cases is not supported.

#### Rust runner {#rust-runner-custom-arg}

The following flags are restricted and the test fails if any are passed as
fuchsia.test.Suite provides equivalent functionality that replaces them.

- **<test_name_matcher>** - Instead use:

```posix-terminal
 fx test --test-filter=<glob_pattern> <test_url>
```

`--test-filter` may be specified multiple times. Tests that match any of the
given glob patterns will be executed.

- **--nocapture** - Output is printed by default.
- **--list** - Listing test cases is not supported.

#### Go test runner {#gotest-runner-custom-arg}

Note the following known behavior change:

**-test.failfast**: As each test case is executed in a different process, this
flag will only influence sub-tests.

The following flags are restricted and the test fails if any are passed as
fuchsia.test.Suite provides equivalent functionality that replaces them

- **-test.run** - Instead use:

```posix-terminal
 fx test --test-filter=<glob_pattern> <test_url>
```

`--test-filter` may be specified multiple times. Tests that match any of the
given glob patterns will be executed.

- **-test.count** - See [Running test multiple times](#running_test_multiple_times).
- **-test.v** - Output is printed by default.
- **-test.parallel** - See [Controlling parallel execution of test cases](#controlling_parallel_execution_of_test_cases).

### A runtime-agnostic, runtime-inclusive testing framework {#inclusive}

Fuchsia aims to be [inclusive][inclusive], for instance in the sense that
developers can create components (and their tests) in their language and runtime
of choice. The Test Runner Framework itself is language-agnostic by design, with
individual test runners specializing in particular programming languages or test
runtimes and therefore being language-inclusive. Anyone can create and use new
test runners.

Creating new test runners is relatively easy, with the possibility of sharing
code between different runners. For instance, the GoogleTest runner and the Rust
runner share code related to launching an ELF binary, but differ in code for
passing command line arguments to the test and parsing the test's results.

## Temporary storage

To use temporary storage in your test, add the following to your component manifest:

```json5
{
    include: [ "//src/sys/test_runners/tmp_storage.shard.cml" ]
}
```

At runtime, your test will have read/write access to `/tmp`.
The contents of this directory will be empty when the test starts, and will be
deleted after the test finishes.

[Tests that don't specify a custom manifest][component-unit-tests] and instead
rely on the build system to generate their component manifest can add the
following dependency:

```gn
fuchsia_unittest_package("foo-tests") {
  deps = [
    ":foo_test",
    "//src/sys/test_runners:tmp_storage",
  ]
}
```

## Hermeticity

A test is *hermetic* if it [uses][manifests-use] or [offers][manifests-offer] no
capabilities from the [test root's](#tests-as-components) parent. As a rule of
thumb, tests should be hermetic, but sometimes a test requires a capability that
cannot be injected in the test realm.

In the context of hermetic tests, a capability that originates from outside of
the test's realm is called a *system capability*.

There are some capabilities which all tests can use which do not violate test
hermeticity:

| Protocol | Description |
| -----------| ------------|
| `fuchsia.boot.WriteOnlyLog` | Write to kernel log |
| `fuchsia.logger.LogSink` | Write to syslog |
| `fuchsia.process.Launcher` | Launch a child process from the test package |
| `fuchsia.sys2.EventSource` | Access to event protocol |

To use these capabilities, there should be a use declaration added to test's
manifest file:

```json5
// my_test.cml
{
    use: [
        ...
        {
            protocol: [
              "{{ '<var label="protocol">fuchsia.logger.LogSink</var>' }}"
            ],
        },
    ],
}
```

Tests are also provided with some default storage capabilities which are
destroyed after the test finishes execution.

| Storage Capability | Description | Path |
| ------------------ | ----------- | ---- |
|  `data` | Isolated data storage directory | `/data` |
|  `cache` | Isolated cache storage directory | `/cache` |
|  `temp` | Isolated in-memory [temporary storage directory](#temporary_storage) | `/tmp` |

Add a use declaration in test's manifest file to use these capabilities.

```json5
// my_test.cml
{
    use: [
        ...
        {
            storage: "{{ '<var label="storage">data</var>' }}",
            path: "{{ '<var label="storage path">/data</var>' }}",
        },
    ],
}
```

The framework also provides some [capabilities][framework-capabilities] to all the
components and can be used by test components if required.

## Performance

When writing a test runner that launches processes, the runner needs to
provide a [library loader][loader-service] implementation.

Test runners typically launch individual test cases in separate processes to
achieve a greater degree of isolation between test cases. However this can come
at a significant performance cost. To mitigate this, the test runners listed
above use a [caching loader service][caching-loader-service] which reduces the
extra overhead per process launched.

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

## Further reading

- [Complex topologies and integration testing][integration-testing]: testing
  interactions between multiple components in isolation from the rest of the
  system.

[cf]: /docs/concepts/components/v2/
[component-manifest]: /docs/concepts/components/v2/component_manifests.md
[component-unit-tests]: /docs/development/components/build.md#unit-tests
[fidl-test-manager]: /sdk/fidl/fuchsia.test.manager/test_manager.fidl
[fidl-test-suite]: /sdk/fidl/fuchsia.test/suite.fidl
[ffx]: /docs/development/tools/ffx/overview.md
[fx-test]: https://fuchsia.dev/reference/tools/fx/cmd/test
[inclusive]: /docs/concepts/principles/inclusive.md
[integration-testing]: /docs/concepts/testing/v2/integration_testing.md
[manifests-offer]: /docs/concepts/components/v2/component_manifests.md#offer
[manifests-use]: /docs/concepts/components/v2/component_manifests.md#use
[runners]: /docs/concepts/components/v2/capabilities/runners.md
[test-suite-protocol]: /docs/concepts/components/v2/realms.md
[unit-tests]: /docs/development/components/build.md#unit_tests_with_generated_manifests
[loader-service]: /docs/concepts/booting/program_loading.md#the_loader_service
[caching-loader-service]: /src/sys/test_runners/src/elf/elf_component.rs
[framework-capabilities]: /docs/concepts/components/v2/component_manifests.md#framework-protocols
[sys-migration-guide]: /docs/development/components/v2/migration.md
