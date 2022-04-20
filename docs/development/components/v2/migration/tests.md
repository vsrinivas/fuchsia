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
system capability, it also needs to be marked with `type: "system"` as shown
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
        include: [
            // If using Wrapped CMX component.
            "sys/testing/hermetic-tier-2-test.shard.cml",
        ],
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

## `TestWithEnvironment`

The legacy Component Framework provided a C++ library named
`TestWithEnvironment` that allowed the construction of an isolated environment
within a test. It was often used to serve injected services that are either
implemented in-process or by other components in the test.

Legacy tests relying on this functionality should move to using [realm
builder][realm-builder] when being migrated. Realm Builder can create isolated
environments similar to the ones constructed by `TestWithEnvironment`. General
usage of the library is covered in the [Realm Builder
documentation][realm-builder]. The remainder of this section covers how to
translate `TestWithEnvironment` specific use cases to Realm Builder.

### Test setup

There is no Realm Builder specific fixture, so any tests that depend on the
`TextWithEnvironmentFixture` portion of the library should instead use a more
generic test fixture, such as `gtest::RealLoopFixture`.

```
class RealmBuilderTest : public gtest::RealLoopFixture {};
```

In the test implementation (or a custom test fixture) the test will call
`RealmBuilder::Create` to begin building a realm, add components and routes to
the realm, and finally call `Build` to construct and begin running the realm
that's been described.

```
TEST_F(RealmBuilderTest, RoutesProtocolFromChild) {
      auto realm_builder = RealmBuilder::Create();
      // Set up the realm.
      ...
      auto realm = realm_builder.Build(dispatcher());
      // Use the constructed realm to assert some property about the components
      // under test.
      ...
}
```

### Adding components to a realm

The legacy components that were added to the nested environment with
`TestWithEnvironment` can be added to the realm created by Realm Builder. A
realm created by Realm Builder can contain both legacy and modern components
simultaneously.

When using `TestWithEnvironment`, the services which are present in a realm are
specified when the legacy realm is created, and after realm creation has
occurred components may be added to the realm (with
`EnclosingEnvironment::CreateComponentFromUrl`). In the modern component
framework, and thus when using Realm Builder, the components are added to a
realm (along with capability routing) as the realm is created, and once the
realm exists its contents are immutable.

Additionally, unlike in `TestWithEnvironment`, when using Realm Builder the
components should be added to the realm _before_ the routing (i.e. protocol
wiring) is performed.

Modern components can be added to a realm with the `AddChild` call:

```
realm_builder->AddChild("example_component", "#meta/example_component.cm");
```

And legacy components can be added to a realm with the `AddLegacyChild` call:

```
realm_builder->AddLegacyChild(
    "example_legacy_component",
    "fuchsia-pkg://fuchsia.com/example#meta/example.cmx");
```

Whenever a component is added to a realm, a name for the component must be
provided. This name is used later in `AddRoute` calls.

When a component is added to a realm, it may also be added along with options
for the new child component. One such option is to mark the component as
`eager`, which causes the component to start running the moment the realm is
created. When this option is not supplied, the component will not begin running
until something accesses a capabilities provided by the component.

```
realm_builder->AddChild(
    "example_eager_component",
    "#meta/example_eager.cm",
    ChildOptions{.startup_mode = StartupMode::EAGER});
```

### Connecting components together

When using `TestWithEnvironment` the parent is responsible for setting up a
`sys::testing::EnvironmentServices` that would hold the set of additional
services available to legacy components in the nested environment. Along with
the services added to the realm with this approach, by default the nested realm
would inherit all services in the parent realm.

When using Realm Builder, the protocols each component expects to be able to
access (along with where it comes from) are added during realm construction by
using the `AddRoute` call. This capability routing must be performed even for
capabilities in the parent realm, unlike in `TestWithEnvironment` where the
components in the nested realm could implicitly access these services.

For example, to make the `fuchsia.logger.LogSink` protocol available to the
`example_component` and `example_legacy_component` components, it must be
explicitly routed to them like so:

```
realm_builder->AddRoute(
    Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
          .source = ParentRef(),
          .targets = {
              ChildRef{"example_component"},
              ChildRef{"example_legacy_component"}}});
```

Connections between children in the realm under construction must also be added
to the realm using the same `AddRoute` call, but with `ChildRef`s as both the
source and target:

```
realm_builder->AddRoute(
    Route{.capabilities = {Protocol{"fuchsia.examples.Example"}},
          .source = ChildRef{"example_component"},
          .targets = {ChildRef{"example_legacy_component"}}});
```

Finally, if the test constructing the realm wishes to be able to access any
capabilities from any of the components in the realm, then those capabilities
must be routed to the parent.

```
realm_builder->AddRoute(
    Route{.capabilities = {Protocol{"fuchsia.examples.Example2"}},
          .source = ChildRef{"example_legacy_component"},
          .targets = {ParentRef{}}});
```

### Implementing protocols

The services held in a `sys::testing::EnvironmentServices` may be implemented
anywhere, including in the test component exercising `TestWithEnvironment`. It's
not possible to offer capabilities implemented in the test component itself
directly to components in the created realm; instead the test component can
create _local components_. These components, instead of being implemented by a
dedicated process, are implemented in-process by local objects. These functions
are added to a realm being constructed, capabilities can be routed to and from
them, and when the realm is created the functions are invoked as dedicated
components.

As an example, if we wanted to implement a mock for the `fuchsia.example.Echo`
protocol:

```
class LocalEchoServer : public test::placeholders::Echo, public LocalComponent {
 public:
  explicit LocalEchoServer(fit::closure quit_loop, async_dispatcher_t* dispatcher)
      : quit_loop_(std::move(quit_loop)), dispatcher_(dispatcher), called_(false) {}

  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
    called_ = true;
    quit_loop_();
  }

  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);
    ASSERT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)),
              ZX_OK);
  }

  bool WasCalled() const { return called_; }

 private:
  fit::closure quit_loop_;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  bool called_;
  std::unique_ptr<LocalComponentHandles> handles_;
};
```

The above class can be instantiated to provide a `fuchsia.example.Echo`
implementation to a realm, and the created object can even be inspected by the
test to determine things like if the FIDL protocol provided by this local
component was accessed.

```
LocalEchoServer local_echo_server(QuitLoopClosure(), dispatcher());
realm_builder.AddLocalChild(kEchoServer, &local_echo_server);
```

## Migrate component features {#features}

Explore the following sections for additional migration guidance on
specific features your test components may support:

-   [Component sandbox features](features.md)
-   [Diagnostics capabilities](diagnostics.md)
-   [Other common situations](common.md)

## Verify the migrated tests {#verify-tests}

Verify that your migrated tests are passing successfully using Components v2.

1.  Build the target for your test package:

    ```posix-terminal
    fx build
    ```

1.  Verify your tests successfully pass with the test:

    ```posix-terminal
    fx test my_component_tests
    ```

    Note: If tools or scripts invoke your tests component using
    `fx shell run-test-component`, migrate this usage to
    `fx shell run-test-suite` or `ffx test run`.

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
[realm-builder]: /docs/development/testing/components/realm_builder.md
