# Tests as components

Caution: This document describes manifests for [components v1][components-v1].
The old architecture (components v1) implemented by `appmgr` is still in use,
but will be removed once the transition to the new architecture is complete.

This document outlines the way for developers to configure their tests to run as
Fuchsia components. This document is for developers working inside the Fuchsia
source tree (`fuchsia.git`). The workflow described in this document is not
suitable for the Fuchsia SDK-based developers.

The exact GN invocations used to produce a test may vary between different
classes of tests and different languages. This document assumes that test logic
is being built somewhere and the test binary is to be built to run as a Fuchsia
component. For C++ and Rust, this would be the executable file that the build
produces. (For information on writing tests in Rust, see
[Testing Rust code][rust-testing].)

For an example setup of a test component, see
<code>[//examples/hello_world/rust][hello-world-rust]</code>.

## Fuchsia test environments

Tests on Fuchsia can run either as standalone executables (for example,
[host tests][run-fuchsia-test]) or as components. Standalone executables are
invoked within the environment the test runner is in, whereas components
executed in a test runner run in a hermetic environment. These hermetic
environments are fully separated from the host services. Test manifests
determine whether new instances of services should be started in this
environment, or services from the host should be plumbed in to the test
environment.

## Packaging a test as a component

To package a test as a component, you need to create the following:

*   [Component manifest](#component-manifest-for-a-test).
*   [Package build rules](#package-build-rules-for-a-test-component) for the
    test executable and manifest.

Note: When writing simple unit tests that do not depend on any facets, features,
or services in Fuchsia, you can create a test package using the
<code>[fuchsia_unittest_package][fuchsia-unittest-package-gni]</code> template.
This template lets the build system automatically generate a component manifest
for these tests.

### Component manifest for a test {#component-manifest-for-a-test}

A component manifest informs the component framework how to run a component. In
this case, it explains how to run the test binary. A component manifest file
(`.cmx`) is typically located in a `meta` directory next to the `BUILD.gn` file:

```none
<your_test_directory>
  ├── BUILD.gn
  ├── meta
  │   └── <component_manifest_file>
  ...
```

When a package is built, it includes the component manifest file under a top
level directory, which is also called `meta`.

The simplest possible component manifest for running a test may look like the
following:

```none
{
    "program": {
        "binary": "test/hello_world_rust_bin_test"
    }
}
```

When you run the component above, it invokes the
`test/hello_world_rust_bin_test` binary in the package.

However, the example manifest above may be inadequate for many use cases because
the program running under this manifest has a very limited set of capabilities.
For instance, there is no mutable storage available for this program and it
cannot access any services in Fuchsia.

#### Sandbox

The `sandbox` portion of the manifest can be used to expand on this. As an
alternative to the prior example, the following example provides the component
access to storage at `/cache` and allows the component to talk to the service
located at `/svc/fuchsia.logger.LogSink`:

```none
{
    "program": {
        "binary": "test/hello_world_rust_bin_test"
    },
    "sandbox": {
        "features": [ "isolated-cache-storage" ],
        "services": [ "fuchsia.logger.LogSink" ]
    }
}
```

Test components can also have new instances of services created inside their
test environment, thus isolating the impact of the test from the host. In the
following example, the service available at `/svc/fuchsia.example.Service` is be
handled by a brand new instance of the service referenced by the URL:

```none
{
    "program": {
        "binary": "test/hello_world_rust_bin_test"
    },
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.example.Service": "fuchsia-pkg://fuchsia.com/example#meta/example_service.cmx"
            }
        }
    },
    "sandbox": {
        "services": [
            "fuchsia.example.Service"
        ]
    }
}
```

For more information on component manifests, see
[Component manifests][component-manifest].

### Package build rules for a test component {#package-build-rules-for-a-test-component}

Once you have a component manifest, you can now add GN build rules in `BUILD.gn`
to create a package for the test component.

The following example produces a new package named `hello-world-rust-tests` that
contains the artifacts necessary to run a test component:

Note: The legacy GN templates for packages (`package.gni` and `test_package.gni`)
are being deprecated. See
[New GN templates for components v1](#new-gn-templates-for-components-v1)
for details.

```none
import("//src/sys/build/fuchsia_unittest_package.gni")

fuchsia_unittest_component("hello-world-rust-test-component") {
  executable_path = "test/hello_world_rust_bin_test"
  component_name = "hello-world-rust-test"
  deps = [ ":bin" ]
}

fuchsia_test_package("hello-world-rust-tests") {
  test_components = [
    ":hello-world-rust-test-component",
  ]
}
```

The example above requires that the `:bin` target produces a test binary named
`hello_world_rust_bin_test`. The
<code>[fuchsia_unittest_package][fuchsia-unittest-package-gni]</code>
template requires that `meta/${TEST_NAME}.cmx` exists and that the destination
of the test binary matches the target name. In the example, this means that
`meta/hello_world_rust_bin_test.cmx` must exist.

This template produces a package in the same way that the
<code>[fuchsia_package][fuchsia-package-gni]</code> template does, but it
has extra checks in place to ensure that the test is set up properly. For
more information, see [Test packages][test-packages].

## New GN templates for components v1 {#new-gn-templates-for-components-v1}

The legacy GN templates for packages
(<code>[test_package.gni][test-package-gni]</code> and
<code>[package.gni][package-gni]</code>) are being deprecated in favor
of the following new templates:

*   <code>[fuchsia_package.gni][fuchsia-package-gni]</code>
*   <code>[fuchsia_test_package.gni][fuchsia-test-package-gni]</code>
*   <code>[fuchsia_unittest_package.gni][fuchsia-unittest-package-gni]</code>

For instance, the examples below demonstrate how you can rewrite
the `test_package` template's build rules using the
`fuchsia_unittest_package` template:

*   {`test_package`}

    ```none
    import("//build/test/test_package.gni")

    test_package("hello-world-rust-tests") {
      deps = [
        ":bin",
      ]
      tests = [
        {
          name = "hello_world_rust_bin_test"
        }
      ]
    }
    ```

*    {`fuchisa_unittest_package`}

    ```none
    import("//src/sys/build/fuchsia_unittest_package.gni")

    fuchsia_unittest_component("hello-world-rust-test-component") {
      executable_path = "test/hello_world_rust_bin_test"
      component_name = "hello-world-rust-test"
      deps = [ ":bin" ]
    }

    fuchsia_test_package("hello-world-rust-tests") {
      test_components = [
        ":hello-world-rust-test-component",
      ]
    }
    ```

For more examples on these templates, see [Test component][test-component].

<!-- Reference links -->

[components-v1]: /docs/glossary.md#components-v1
[hello-world-rust]: /examples/hello_world/rust
[run-fuchsia-test]: /docs/development/testing/run_fuchsia_tests.md
[component-manifest]: /docs/concepts/components/v1/component_manifests.md
[rust-testing]: /docs/development/languages/rust/testing.md
[test-package-gni]: /build/test/test_package.gni
[package-gni]: /build/package.gni
[test-packages]: /docs/development/components/build.md#test-packages
[fuchsia-package-gni]: /src/sys/build/fuchsia_package.gni
[fuchsia-test-package-gni]: /src/sys/build/fuchsia_test_package.gni
[fuchsia-unittest-package-gni]: /src/sys/build/fuchsia_unittest_package.gni
[test-component]: /docs/concepts/testing/test_component.md
