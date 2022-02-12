# Migrate test components

To migrate your test components, follow these steps:

1.  [Migrate the test manifest](#create-test-manifest)
1.  [Update test dependencies](#update-dependencies)
1.  [Migrate component features](#features)
1.  [Verify the migrated tests](#verify-tests)

## Migrate the test manifest {#create-test-manifest}

Find the GN build rules for the tests that exercise your component.
Typically this is a [`fuchsia_test_package()`](#test-package) or
[`fuchsia_unittest_package()`](#unittest-package).

### Unit test packages {#unittest-package}

The preferred practice for tests declared with a `fuchsia_unittest_package()`
build rule is to use the [generated manifest][unit-test-manifests] provided by
the Fuchsia build system.

To allow the GN target to generate your manifest, remove the `manifest`
attribute from the `fuchsia_unittest_package()`:

```gn
fuchsia_unittest_package("my_component_tests") {
  {{ '<strike>' }}manifest = "meta/my_component_test.cmx"{{ '</strike>' }}
  deps = [ ":my_component_test" ]
}
```

Your test package is now able to execute using Components v2 and the
Test Runner Framework.

### Test packages {#test-package}

Consider the following example test component manifest:

```json
// my_component_test.cmx
{
    "include": [
        "syslog/client.shard.cmx"
    ],
    "program": {
        "binary": "bin/my_component_test"
    }
}
```

To migrate this test to the Test Runner Framework, do the following:

1.  Create a CML file that points to the test binary that includes the
    appropriate [test runner][trf-test-runners]:

    Note: See the [available test runners][trf-provided-test-runners] provided
    by the framework.

    ```json5
    // my_component_test.cml
    {
        include: [
            // Select the appropriate test runner shard here:
            // rust, gtest, go, etc.
            "//src/sys/test_runners/rust/default.shard.cml",
            // Enable system logging
            "syslog/client.shard.cml",
        ],
        program: {
            binary: "bin/my_component_test",
        }
    }
    ```

1.  Locate the GN build rule for your test component referenced by the
    `fuchsia_test_package()`:

    ```gn
    fuchsia_component("my_component_test") {
      testonly = true
      manifest = "meta/my_component_test.cmx"
      deps = [ ":bin_test" ]
    }

    fuchsia_test_package("my_component_tests") {
      deps = [ ":my_component_test" ]
    }
    ```

1.  Update your test component's build rule to reference the new CML file:

    ```gn
    fuchsia_component("my_component_test") {
      testonly = true
      {{ '<strong>' }}manifest = "meta/my_component_test.cml"{{ '</strong>' }}
      deps = [ ":bin_test" ]
    }

    fuchsia_test_package("my_component_tests") {
      deps = [ ":my_component_test" ]
    }
    ```

## Update test dependencies {#update-dependencies}

A test may include or depend on components that are separate from the test
component. Here are some things to look for:

-   Does your test have a CMX with [`fuchsia.test facets`][fuchsia-test-facets],
    such as `injected-services` or `system-services`?
-   Does your test create environments in-process? If so, does it create a
    separate environment for each test case?

Note: The Test Runner Framework executes tests within a realm that enforces
hermetic component resolution, which means that test components must resolve
dependencies from within their own package.
For more details, see [hermetic component resolution][hermetic-resolution].

The migration procedure varies depending on the testing framework features in
your v1 component:

-   [Test depends on system services](#system-services): The test has a CMX that
    contains [`system-services`][system-services] test facets.
-   [Test depend on injected services](#injected-services): The test has a CMX that
    contains [`injected-services`][fuchsia-test-facets] test facets.

Note: For more details on the services and capabilities provided to components
by the Test Runner Framework, see the
[test manager documentation][trf-test-manager].

### System service dependencies {#system-services}

For tests that use [`system-services`][system-services] test facets, consider if
they can be converted to [injected services](#injected-services) instead.
Injecting services is the preferred method because it promotes hermetic test
behavior.

For certain non-hermetic tests, the Test Runner Framework provides the test
realm with the following services:

| Service                             | Description                           |
| ----------------------------------- | ------------------------------------- |
| `fuchsia.scheduler.ProfileProvider` | Profile provider for scheduler        |
| `fuchsia.sysmem.Allocator`          | Allocates system memory buffers       |
| `fuchsia.tracing.provider.Registry` | Register to trace provider            |
| `fuchsia.vulkan.loader.Loader`      | Vulkan library provider               |
| `fuchsia.sys.Loader`                | CFv1 loader service to help with      |
:                                     : migration.                            :
| `fuchsia.sys.Environment`           | CFv1 environment service to help with |
:                                     : migration.                            :

Consider the following example test component that uses a single system service,
`fuchsia.sysmem.Allocator`:

```json
// my_component_test.cmx
{
    "facets": {
        "fuchsia.test": {
            "system-services": [
                "fuchsia.sysmem.Allocator"
            ]
        }
    },
    "program": {
        "binary": "bin/my_component_test"
    },
    "sandbox": {
        "services": [
            "fuchsia.sysmem.Allocator"
        ]
    }
}
```

To migrate this test to the Test Runner Framework, declare each available system
service with the other [required services](#required-services) in your test
component manifest. Since this test uses the `fuchsia.sysmem.Allocator`
system capability, it also needs to be marked as `hermetic: "false"` as shown
below.

```json5
// my_component_test.cml

{
    include: [
        // Select the appropriate test runner shard here:
        // rust, gtest, go, etc.
        "//src/sys/test_runners/rust/default.shard.cml",
    ],
    program: {
        binary: "bin/my_component_test",
    },
    {{ '<strong>' }}facets: {
        "fuchsia.test": {
            type: "system"
        },
    },
    use: [
        {
            protocol: [ "fuchsia.sysmem.Allocator" ],
        },
    ],{{ '</strong>' }}
}
```

### Injected service dependencies {#injected-services}

For tests that use other [fuchsia.test facets][fuchsia-test-facets], such as
`injected-services`, your test component manifest must declare each dependent
component and route the provided capabilities to the test component.

In the following example, suppose there's a single injected service,
`fuchsia.pkg.FontResolver`:

```json
// my_component_test.cmx
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.pkg.FontResolver":
                    "fuchsia-pkg://fuchsia.com/font_provider_test#meta/mock_font_resolver.cmx"
            }
        }
    },
    "program": {
        "binary": "bin/my_component_test"
    },
    "sandbox": {
        "services": [
            "fuchsia.pkg.FontResolver"
        ]
    }
}
```

To migrate this test to the Test Runner Framework, do the following:

1.  Create a CML file for the test component that points to the test binary and
    includes the appropriate [test runner][trf-test-runners]:

    Note: See [test runners][trf-provided-test-runners] that are provided by the
    framework.

    ```json5
    // my_component_test.cml (test component)
    {
        include: [
            // Select the appropriate test runner shard here:
            // rust, gtest, go, etc.
            "//src/sys/test_runners/rust/default.shard.cml",
        ],
        program: {
            // Binary containing tests
            binary: "bin/font_provider_test",
        },
        use: [
            ...
        ],
    }
    ```

1.  You need CML files for each component that provides a capability needed in
    the test. If there is an existing CML file for the component providing the
    injected service, you may be able to reuse it. Otherwise **if the mock
    component is being ported to v2**, create a new CML file.  If the **mock
    component has not been ported to v2 yet**, wrap the component using
    `cmx_runner`.

    * {CML component}

        ```json5
        // mock_font_resolver.cml (capability provider)
        {
            program: {
            runner: "elf",
            binary: "bin/mock_font_resolver",
            },
            use: [
                //  mock_font_resolver's dependencies.
                {
                    protocol: [ "fuchsia.proto.SomeProtocol" ],
                },
            ],
            capabilities: [
                {
                    protocol: [ "fuchsia.pkg.FontResolver" ],
                },
            ],
            expose: [
                {
                    protocol: "fuchsia.pkg.FontResolver",
                    from: "self",
                },
            ],
        }
        ```

    * {Wrapped CMX component}

        ```json5
        // mock_font_resolver.cml (capability provider)
        {
            include: [
                // Use `cmx_runner` to wrap the component.
                "//src/sys/test_manager/cmx_runner/default.shard.cml",
                "syslog/client.shard.cml",
            ],
            program: {
                // wrap v1 component
                legacy_url: "fuchsia-pkg://fuchsia.com/font_provider_test#meta/mock_font_resolver.cmx",
            },
            use: [
                // if `mock_font_resolver.cmx` depends on some other protocol.
                {
                    protocol: [ "fuchsia.proto.SomeProtocol" ],
                },
                // Note: Wrapped legacy component manifest can only "use"
                // protocol capabilities. cmx file of the legacy component will
                // define what other non-protocol capabilities it gets (example
                // isolated-tmp, /dev, etc). These capabilities will come
                // directly from the system and can't be mocked or forwarded
                // from the test to legacy components.
            ],
            // expose capability provided by mock component.
            capabilities: [
                {
                    protocol: [ "fuchsia.pkg.FontResolver" ],
                },
            ],
            expose: [
                {
                    protocol: "fuchsia.pkg.FontResolver",
                    from: "self",
                },
            ],
        }
        ```

    Note: The CML files for the capability providers can be distributed in the
    same package that contained the v1 test. Follow the same instructions in
    [Migrate the component manifest][migrate-component-manifest] that you used
    to package your component.

1.  Add the capability provider(s) as children of the test component, and route
    the capabilities from each provider.

    ```json5
    // my_component_test.cml (test component)
    {
        ...

        // Add capability providers
        children: [
            {
                name: "font_resolver",
                url: "#meta/mock_font_resolver.cm",
            },
        ],
        // Route capabilities to the test
        use: [
            {
                protocol: [ "fuchsia.pkg.FontResolver" ],
                from: "#font_resolver",
            },
        ],
        offer: [
            {
                // offer dependencies to mock font provider.
                protocol: [ "fuchsia.proto.SomeProtocol" ],
                from: "#some_other_child",
            },
        ],
    }
    ```

1.  Package the test component and capability provider(s) together into a
    single hermetic `fuchsia_test_package()`:

    * {CML component}

        ```gn
        # Test component
        fuchsia_component("my_component_test") {
        testonly = true
        manifest = "meta/my_component_test.cml"
        deps = [ ":bin_test" ]
        }

        fuchsia_component("mock_font_resolver") {
        testonly = true
        manifest = "meta/mock_font_resolver.cml"
        deps = [ ":mock_font_resolver_bin" ]
        }

        # Hermetic test package
        fuchsia_test_package("my_component_tests") {
        test_components = [ ":my_component_test" ]
        deps = [ ":mock_font_resolver" ]
        }
        ```

    * {Wrapped CMX component}

        ```gn
        # Test component
        fuchsia_component("my_component_test") {
        testonly = true
        manifest = "meta/my_component_test.cml"
        deps = [ ":bin_test" ]
        }

        fuchsia_component("mock_font_resolver") {
        testonly = true
        manifest = "meta/mock_font_resolver.cml"
        deps = [ {{ '<var label="legacy_component">"//path/to/legacy(v1)_component"</var>' }} ]
        }

        # Hermetic test package
        fuchsia_test_package("my_component_tests") {
        test_components = [ ":my_component_test" ]
        deps = [ ":mock_font_resolver" ]
        }
        ```

For more details on providing external capabilities to tests, see
[Integration testing topologies][integration-test].

## Migrate component features {#features}

Explore the following sections for additional migration guidance on
specific features your test components may support:

-   [Component sandbox features](features.md)
-   [Diagnostics capabilities](diagnostics.md)
-   [Other common situations](common.md)

## Verify the migrated tests {#verify-tests}

Build and run your test and verify that it passes:

```posix-terminal
fx build && fx test my_component_tests
```

If your test doesn't run correctly or doesn't start at all, try following the
advice in [Troubleshooting components][troubleshooting-components].

[example-package-rule]: https://fuchsia.googlesource.com/fuchsia/+/cd29e692c5bfdb0979161e52572f847069e10e2f/src/fonts/BUILD.gn
[fuchsia-test-facets]: /docs/concepts/testing/v1_test_component.md
[hermetic-resolution]: /docs/development/testing/components/test_runner_framework.md#hermetic_component_resolution
[integration-test]: /docs/development/testing/components/integration_testing.md
[migrate-component-manifest]: /docs/development/components/v2/migration/components.md#create-component-manifest
[system-services]: /docs/concepts/testing/v1_test_component.md#services
[trf-provided-test-runners]: /src/sys/test_runners
[trf-test-manager]: /docs/development/testing/components/test_runner_framework.md#the_test_manager
[trf-test-runners]: /docs/development/testing/components/test_runner_framework.md#test-runners
[troubleshooting-components]: /docs/development/components/troubleshooting.md
[unit-test-manifests]: /docs/development/components/build.md#unit-tests