# Tests as components

Tests on Fuchsia can either be run as standalone executables or as components.
Standalone executables are invoked in whatever environment the test runner
happens to be in, whereas components executed in a test runner are run in a
hermetic environment.

These hermetic environments are fully separated from the host services, and the
test manifests can stipulate that new instances of services should be started in
this environment, or services from the host should be plumbed in to the test
environment.

This document aims to outline the idiomatic way for a developer to configure
their test artifacts to be run as components. This document is targeted towards
developers working inside of `fuchsia.git`, and the workflow described is
unlikely to work for SDK-based developers.

An example setup of a test component is available at
`//examples/hello_world/rust`.

## Building the test

The exact GN invocations that should be used to produce a test vary between
different classes of tests and different languages. The rest of this document
assumes that test logic is being built somewhere, and that the test output is
something that can be run as a component. For C++ and Rust, this would be the
executable file the build produces.

Further documentation for building tests is [available for Rust][rust_testing].

## Packaging and component-ifying the tests

Once the build rule for building a test executable exists, a component manifest
referencing the executable and a package build rule containing the executable
and manifest must be created.

### Component manifests

The component manifest exists to inform the component framework how to run
something. In this case, it's explaining how to run the test binary. This file
typically lives in a `meta` directory next to the `BUILD.gn` file, and will be
included in the package under a top level directory also called `meta`.

The simplest possible component manifest for running a test would look like
this:

```cmx
{
    "program": {
        "binary": "test/hello_world_rust_bin_test"
    }
}
```

This component, when run, would invoke the `test/hello_world_rust_bin_test`
binary in the package.

This example manifest may be insufficient for many use cases as the program will
have a rather limited set of capabilities, for example there will be no mutable
storage available and no services it can access. The `sandbox` portion of the
manifest can be used to expand on this. As an alternative to the prior example,
this example will give the component access to storage at `/cache` and will
allow it to talk to the service located at `/svc/fuchsia.logger.LogSink`.

```cmx
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
following example, the service available at `/svc/fuchsia.example.Service` will
be handled by a brand new instance of the service referenced by the URL.

```cmx
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

For a more thorough description of what is valid in a component manifest, please
see the [documentation on component manifests][component_manifest].

### Component and package build rules

With a component manifest written the GN build rule can now be added to create a
package that holds the test component.

```GN
import("//build/test/test_package.gni")

test_package("hello_world_rust_tests") {
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

This example will produce a new package named `hello_world_rust_tests` that
contains the artifacts necessary to run a test component. This example requires
that the `:bin` target produce a test binary named `hello_world_rust_bin_test`.

The `test_package` template requires that `meta/${TEST_NAME}.cmx` exist and that
the destination of the test binary match the target name. In this example, this
means that `meta/hello_world_rust_bin_test.cmx` must exist. This template
produces a package in the same way that the `package` template does, but it has
extra checks in place to ensure that the test is set up properly. For more
information, please  see the [documentation on `test_package`][test_package].

<!-- Reference links -->

[component_manifest]: /docs/concepts/components/v1/component_manifests.md
[rust_testing]: /docs/development/languages/rust/testing.md
[test_package]: /docs/concepts/testing/test_component.md
[fuchsia_package_url]: /docs/concepts/packages/package_url.md
[glossary-components-v2]: /docs/glossary.md#components-v2
[rust-glob-syntax]: https://docs.rs/glob/0.3.0/glob/struct.Pattern.html
