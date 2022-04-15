# Integration testing

<<../../../_common/fidl/_testing_intro.md>>

## Test components

Below is an example component manifest for a simple integration test component:

`meta/integration_tests.cml`:

```json5
{
    include: [
        "//pkg/syslog/client.shard.cml",
        "//pkg/sys/testing/elf_test_runner.shard.cml",
    ],
    program: {
        binary: "bin/client_test",
    },
    children: [
        {
            name: "service",
            url: "fuchsia-pkg://fuchsia.com/foo-package-tests#meta/mock_service.cm",
        },
        {
            name: "client",
            url: "fuchsia-pkg://fuchsia.com/foo-package-tests#meta/foo_client.cm",
        },
    ],
    offer: [
        {
            protocol: "fuchsia.example.Foo",
            from: "#service",
            to: [ "#client" ],
        },
    ],
}
```

This test component declaration contains the following key elements:

1.  An `include`  of the necessary language-specific test runner shard. This
    enables the test manager to properly execute the test suite.
1.  Listing the component under test and dependent components as `children`.
1.  Routing required capabilities between components in the test realm.

Here is an example of how the above integration test could be included in the
`BUILD.bazel` file:

```bazel
load(
    "fuchsia_component",
    "fuchsia_component_manifest",
    "fuchsia_package",
    "fuchsia_test_component",
)

// Component under test
fuchsia_component(
  name = "foo_client",
  manifest = ":foo_client_manifest",
  visibility = ["//visibility:public"],
)

// Test dependencies
fuchsia_component(
  name = "mock_service",
  manifest = ":mock_service_manifest",
  visibility = ["//visibility:public"],
)

// Component containing integration tests
fuchsia_component_manifest(
  name = "integration_test_manifest",
  src = "meta/integration_tests.cml",
)
fuchsia_test_component(
  name = "integration_test_component",
  manifest = ":integration_test_manifest",
  test_name = "client_integration_test",
  visibility = ["//visibility:public"],
)

fuchsia_package(
    name = "integration_test_pkg",
    visibility = ["//visibility:public"],
    deps = [
      ":foo_client",
      ":mock_service",
      ":integration_test_component",
    ],
)
```

## Exercise: Echo server integration test

In this exercise, you'll add an integration test component to exercise the FIDL
protocol interface of the `echo_server` component with the Test Runner
Framework and run those tests in a FEMU environment.

### Add an integration test component

To begin, create a new project directory structure under
`fuchsia-codelab/echo-integration`:

```none {:.devsite-disable-click-to-copy}
fuchsia-codelab/echo-integration
  |- BUILD.bazel
  |- meta
  |   |- echo_integration_test.cml
  |
  |- echo_integration_test.cc
```

### Update the test component manifest

Update the `echo_integration_test.cml` file to declare the `echo-server`
component as a child and route the `Echo` protocol capability to the test
component.

`echo-integration/meta/echo_integration_test.cml`:

```json5
{% includecode gerrit_repo="fuchsia/sdk-samples/getting-started" gerrit_path="src/routing/integration_tests/meta/echo_integration_test.cml" region_tag="example_snippet" adjust_indentation="auto" %}
```

Notice that the `echo-server` instance comes from the same package as the
integration test. This practice promotes test packages that are **hermetic** by
avoiding dependencies on components from other packages.

Add the necessary Bazel rules to the build configuration for the integration
test component:

`echo-integration/BUILD.bazel`:

```bazel
load(
    "@rules_fuchsia//fuchsia:defs.bzl",
    "fuchsia_cc_binary",
    "fuchsia_component",
    "fuchsia_component_manifest",
    "fuchsia_package",
    "fuchsia_test_component",
)

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "echo_integration_test",
    size = "small",
    srcs = ["echo_integration_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "//fuchsia-codelab/echo-fidl:fidl.examples.routing.echo.fidl_cc",
        "@com_google_googletest//:gtest_main",
        "@fuchsia_sdk//pkg/async-default",
        "@fuchsia_sdk//pkg/async-loop",
        "@fuchsia_sdk//pkg/async-loop-cpp",
        "@fuchsia_sdk//pkg/async-loop-default",
        "@fuchsia_sdk//pkg/fdio",
        "@fuchsia_sdk//pkg/sys_cpp",
        "@fuchsia_sdk//pkg/syslog",
    ],
)

filegroup(
    name = "common_libs",
    srcs = [
        "@fuchsia_sdk//:arch/x64/sysroot/dist/lib/ld.so.1",
    ],
    visibility = ["//visibility:public"],
)

fuchsia_component_manifest(
    name = "test_manifest",
    src = "meta/echo_integration_test.cml",
    component_name = "echo_integration_test_component",
    includes = [
        "@fuchsia_sdk//pkg/syslog:client",
        "@fuchsia_sdk//pkg/sys/testing:elf_test_runner",
    ],
    visibility = ["//visibility:public"],
)

fuchsia_test_component(
    name = "echo_integration_test_component",
    manifest = ":test_manifest",
    test_name = "echo_integration_test",
    visibility = ["//visibility:public"],
    deps = [
        ":common_libs",
        ":echo_integration_test",
        "@fuchsia_clang//:dist",
    ],
)

fuchsia_package(
    name = "test_pkg",
    package_name = "echo_integration_test",
    testonly = True,
    visibility = ["//visibility:public"],
    deps = [
        ":echo_integration_test_component",
        "//fuchsia-codelab/echo-server:echo_server_component",
    ],
)
```

### Implement the integration test

The integration test connects to the `Echo` protocol exposed by the
`echo-server` in the same way as the client component, sends a string request,
and validates the expected response.

Add the following code to implement an integration test:

`echo-integration/echo_integration_test.cc`:

```cpp
{% includecode gerrit_repo="fuchsia/sdk-samples/getting-started" gerrit_path="src/routing/integration_tests/echo_integration_test.cc" region_tag="example_snippet" adjust_indentation="auto" %}
```

### Update the build configuration

Run `bazel build` and verify that the build completes successfully:

```posix-terminal
bazel build --config=fuchsia_x64 //fuchsia-codelab/echo-integration:test_pkg \
     --publish_to=$HOME/.package_repos/sdk-samples
```

### Run the integration test

The `fuchsia_test_package()` rule generates a package with the test component
and its dependencies. The integration test component has the following URL:

```none
fuchsia-pkg://fuchsiasamples.com/echo_integration_test#meta/echo_integration_test.cm
```

Use the `ffx test` command to execute the integration tests. Verify that the
tests pass:

```posix-terminal
ffx test run \
    fuchsia-pkg://fuchsiasamples.com/echo_integration_test#meta/echo_integration_test.cm
```
