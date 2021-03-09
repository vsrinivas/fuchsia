# Migrating system components

This document provides instructions for migrating a system component from
[Components v1][glossary-components-v1] to [Components v2][glossary-components-v2].
A *system component* is a component that exists to provide services to other
components in the system.
Typically, in Components v1 the mapping of service to component is registered
in a [sysmgr configuration file][sysmgr-config].

To migrate your system component from v1 to v2, do the following:

-   [Prerequisites](#prerequisites)
-   [Migrate the component manifest](#create-component-manifest)
-   [Migrate the tests](#migrate-tests)

Depending on the features your component supports, you may need to explore the
following sections for additional guidance:

-   [Other common capabilities](#other-capabilities)
-   [Converting CMX features](#cmx-features)

For more details on the components migration effort, see
[State of the Components v2 Migration][components-migration-status].

## Prerequisites {#prerequisites}

Before you begin, ensure that your component uses the latest build templates.
If your component still uses the legacy `package()` in your `BUILD.gn`,
[migrate your package templates][build-migration] before continuing.

You should also familiarize yourself with the following topics:

-   [Introduction to the Fuchsia Component Framework][components-intro]:
    Components v2 comprises a set of concepts and APIs that are
    distinct from Components v1 or traditional OS program models.
-   [Introduction to the Test Runner Framework][ftf-intro]:
    Test Runner Framework is built on the Component Framework. You need to be
    familiar with these concepts before you migrate tests.

## Migrate the component manifest {#create-component-manifest}

Create a minimal [CML file][glossary-component-manifest] and configure it
with GN so that it gets compiled and installed in your package.

Note: Unlike CMX, CML is JSON5, which allows comments and trailing commas.
Take advantage of this when writing your CML file!

1.  Determine where your CMX file is located in the source tree
    (for example, [`fonts.cmx`][example-fonts]).
    Create a file in the same directory that has the same filename but with a `.cml`
    extension, with the following contents:

    ```json5
    {
        include: [
            // Enable system logging
            "sdk/lib/diagnostics/syslog/client.shard.cml",
        ],
    }
    ```

    Note: Your CML file will live side-by-side with the CMX file for now.
    Do not delete the CMX file yet.

1.  Find the build rule that defines your component. Normally, this is a
    `fuchsia_component` rule. For example, see the fonts
    [`BUILD.gn`][example-package-rule].

    ```gn
    fuchsia_component("fonts") {
      manifest = "meta/fonts.cmx"
      deps = [ ":font_provider" ]
    }
    ```

1.  Update the `manifest` element of the associated `fuchsia_component` rule to
    point to your new `.cml` file instead:

    ```gn
    fuchsia_component("fonts") {
      manifest = "meta/fonts.cml"
      deps = [ ":font_provider" ]
    }
    ```

1.  Build the target for your package:

    ```posix-terminal
    fx build
    ```

You are ready to start writing your v2 component manifest.

### Adding the executable {#component-executable}

Add the [`program`][manifests-program] section of your CML file along with the
appropriate runner declaration.

Note: The [runner][glossary-runner] declaration is necessary even if your
component is launched using the ELF runner. This is the default in CMX but must
be explicitly specified in CML.

```json5
// fonts.cmx
{
    "program": {
        "binary": "bin/font_provider"
    }
    ...
}
```

```json5
// fonts.cml
{
    program: {
        runner: "elf",
        binary: "bin/font_provider",
    }
}
```

### Declaring required services {#required-services}

Add [`use`][manifests-use] declarations to your CML file. These are the
approximate equivalent of the [`services`][cmx-services] list in CMX.

```json5
// fonts.cmx
{
    "program": {
        "binary": "bin/app"
    }
    "sandbox": {
        "services": [
            "fuchsia.logger.LogSink",
            "fuchsia.pkg.FontResolver"
        ]
        ...
    }
}
```

Convert each element of the `services` list to a `use` declaration for the
corresponding service `protocol`.

```json5
// fonts.cml
{
    include: [
        // Enable system logging
        "sdk/lib/diagnostics/syslog/client.shard.cml",
    ],
    program: {
      runner: "elf",
      binary: "bin/font_provider",
    },
    use: [
        {
            protocol: [
                "fuchsia.pkg.FontResolver",
            ],
        },
    ],
}
```

### Exposing available services {#available-services}

In [Components v1][glossary-components-v2], you typically declare information
about services exposed by a component in a [sysmgr configuration file][sysmgr-config].
These files are referenced by `config_data` targets in the build, and specify
mappings of services to components in the `sys` [environment][glossary-environment].

Note: The most common location of this service mapping is
[`services.config`][example-services-config], which defines service mappings
that apply to every product configuration.

1.  Identify all service mappings, if any, for your component.
    You can use [CodeSearch][code-search] to find service mappings. Here is a
    [sample search][sysmgr-config-search].

    ```json5
    // services.config
    {
        "services": {
            ...
            "fuchsia.fonts.Provider": "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx",
            ...
        }
    }
    ```

1.  For each service mapping, add an [`expose`][manifests-expose] declaration
    and a corresponding [`capabilities`][manifests-capabilities] entry with the
    service `protocol`.

    ```json5
    // fonts.cml
    {
        include: [
            // Enable system logging
            "sdk/lib/diagnostics/syslog/client.shard.cml",
        ],
        program: {
          runner: "elf",
          binary: "bin/font_provider",
        },
        capabilities: [
            {
                protocol: [ "fuchsia.fonts.Provider" ],
            },
        ],
        use: [
            {
                protocol: [
                    "fuchsia.pkg.FontResolver",
                ],
            },
        ],
        expose: [
            {
                protocol: "fuchsia.fonts.Provider",
                from: "self",
            },
        ],
    }
    ```

1.  Build your updated package:

    ```posix-terminal
    fx build
    ```

1.  Verify that your package includes the compiled v2 component manifest
    (with a `.cm` extension).

    ```posix-terminal
    fx scrutiny -c "search.components --url {{ '<var label="component">my_component.cm</var>' }}$"
    ```

## Migrate the tests {#migrate-tests}

In most cases, tests for v1 components are themselves v1 components. The first
step is to identify all tests that exercise your component’s functionality.
Typically this is a `fuchsia_test_package` or `fuchsia_unittest_package` rule.
For example, see the fonts [`BUILD.gn`][example-package-rule].

A test may include or depend on components that are separate from the test
driver. Here are some things to look for:

-   Is your test self-contained in one file (a unit test)? Or does it launch
    other components (an integration test)?
-   Does your test have a CMX with
    [`fuchsia.test facets`][fuchsia-test-facets]?
-   Does your test create environments in-process? If so, does it create a
    separate environment for each test case?
-   Does your test have a CMX containing
    [`system-services`][system-services]?

### Update the test configuration {#update-test-config}

The migration procedure varies depending on the testing framework features in
your v1 component:

-   [Test has no injected services](#no-injected-services): The test is
    generated by a `fuchsia_unittest_package` GN rule, or its
    CMX does not contain [`injected-services`][fuchsia-test-facets].
-   [Test has injected services](#injected-services): The test has a CMX
    that contains [fuchsia.test facets][fuchsia-test-facets].

#### Test with no injected services {#no-injected-services}

For tests that use no injected services, your [test root][ftf-roles] can be the
same component as the [test driver][ftf-roles].
The v2 test's component manifest should be distributed in the same package
that contains the test binary. Follow the same instructions from
[Migrate the component manifest](#create-component-manifest)
that you used to package your component.

Consider the following example test component:

```json5
// fonts_test.cmx
{
    "program": {
        "binary": "bin/font_test"
    }
}
```

To migrate this test to the v2 testing framework, do the following:

1.  Create a CML file that points to the test binary that includes the
    appropriate [test runner][ftf-test-runners]:

    Note: See [test runners][ftf-provided-test-runners] that are provided by the
    framework.

    ```json5
    // fonts_test.cml
    {
        include: [
            // Select the appropriate test runner shard here:
            // rust, gtest, go, etc.
            "src/sys/test_runners/rust/default.shard.cml",
        ],
        program: {
            binary: "bin/font_test",
        }
    }
    ```

1.  Update the `fuchsia_component` rule for your test component to reference the
    new CML file:

    ```gn
    fuchsia_component("fonts_test_driver") {
      testonly = true
      manifest = "meta/fonts_test.cml"
      deps = [ ":font_test" ]
    }

    fuchsia_test_package("font_provider_tests") {
      test_components = [ ":fonts_test_driver" ]
    }
    ```

#### Test with injected services {#injected-services}

For tests that use [fuchsia.test facets][fuchsia-test-facets], such as
`injected-services`, your [test root][ftf-roles] and [test driver][ftf-roles]
must be split into different components to enable proper capability routing.

In this example, suppose there's a single injected service,
`fuchsia.pkg.FontResolver`:

```json5
// font_provider_test.cmx
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.pkg.FontResolver":
                    "fuchsia-pkg://fuchsia.com/font_provider_tests#meta/mock_font_resolver.cmx"
            }
        }
    },
    "program": {
        "binary": "bin/font_provider_test"
    },
    "sandbox": {
        "services": [
            "fuchsia.pkg.FontResolver"
        ]
    }
}
```

To migrate this test to the v2 testing framework, do the following:

1.  Create a CML file for the test driver that points to the test binary,
    and includes the appropriate [test runner][ftf-test-runners]:

    Note: See [test runners][ftf-provided-test-runners] that are provided by the
    framework.

    ```json5
    // test_driver.cml (test driver)
    {
        include: [
            // Select the appropriate test runner shard here:
            // rust, gtest, go, etc.
            "src/sys/test_runners/rust/default.shard.cml",
        ],
        program: {
            binary: "bin/font_provider_test",
        }
    }
    ```

1.  You need CML files for each component that provides a capability needed
    in the test. If there is an existing CML file for the component providing
    the injected service, you may be able to reuse it.
    Otherwise, create a new CML file.

    ```json5
    // mock_font_resolver.cml (capability provider).
    {
        program: {
          runner: "elf",
          binary: "bin/mock_font_resolver",
        },
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
    [Migrate the component manifest](#create-component-manifest) that you
    used to package your component.

1.  Create a new CML file for the test root that includes the test driver and
    capability provider(s) as children and offers the capabilities from the
    provider(s) to the driver.
    This component should also expose the [test suite protocol][ftf-test-suite].

    ```json5
    // font_provider_test.cml (test root)
    {
        children: [
            {
                name: "test_driver",
                url: "fuchsia-pkg://fuchsia.com/font_integration_test#meta/test_driver.cm",
            },
            {
                name: "font_resolver",
                url: "fuchsia-pkg://fuchsia.com/font_integration_test#meta/mock_font_resolver.cm",
            },
        ],
        expose: [
            {
                protocol: "fuchsia.test.Suite",
                from: "#test_driver",
            },
        ],
        offer: [
            {
                protocol: "fuchsia.pkg.FontResolver",
                from: "#font_resolver",
                to: [ "#test_driver" ],
            },
        ],
    }
    ```

1.  Add `fuchsia_component` rules for each CML file, and update the
    `fuchsia_package` to reference the child components as dependencies:

    ```gn
    fuchsia_component("test_driver") {
      testonly = true
      manifest = "meta/test_driver.cml"
      deps = [ ":font_provider_test_bin" ]
    }

    fuchsia_component("mock_font_resolver") {
      testonly = true
      manifest = "meta/mock_font_resolver.cml"
      deps = [ ":mock_font_resolver_bin" ]
    }

    fuchsia_component("font_provider_test") {
      testonly = true
      manifest = "meta/font_provider_test.cml"
    }

    fuchsia_test_package("font_provider_tests") {
      test_components = [ ":font_provider_test" ]
      deps = [
        ":fonts_test_driver",
        ":mock_font_resolver",
      ]
    }
    ```

### Verify the migrated tests {#verify-tests}

Build and run your test and verify that it passes. Like any other test, use
`fx test` to invoke the test:

```posix-terminal
fx build && fx test font_provider_tests
```

Your component is now tested in Components v2.

If your test doesn't run correctly or doesn't start at all, try following
the advice in [Troubleshooting components][troubleshooting-components].

## Add the new component {#add-component-to-topology}

Note: This section assumes that your component is not in `apps` or
`startup_services`. If it is, reach out to [component-framework-dev][cf-dev-list]
for guidance.

Now you're ready to add your new component to the
[v2 component topology][components-topology]. This defines the relationship
between your component and the rest of the system.

Take another look at any sysmgr configuration file(s) that defines service
mappings to your component, which you identified while
[migrating the component manifest](#create-component-manifest).
The steps below refer to the collection of all these services as your
component’s "exposed services".

```json5
// services.config
{
    "services": {
        ...
        "fuchsia.fonts.Provider": "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx",
        ...
    }
}
```

### Add the component to core {#add-component-to-core}

Add your component as a child instance of the [`core.cml`][cs-core-cml]
component, and offer its exposed services to appmgr. You need to choose
a name for your component instance and identify its component URL (you should
be able to get this from the config mapping).

```json5
// core.cml
{
    children: [
        ...
        {
            name: "font_provider",
            url: "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cm",
        },
    ],
    offer: [
        ...
        {
            protocol: "fuchsia.fonts.Provider",
            from: "#font_provider",
            to: [ "#appmgr" ],
        },
    ],
}
```

### Expose services to sys environment {#expose-services}

Declare each of these services in [`appmgr.cml`][cs-appmgr-cml] to make them
available to v1 components under the `sys` environment.
Change `appmgr.cml` as follows:

```json5
// appmgr.cml
{
    use: [
        ...
        {
            protocol: "fuchsia.fonts.Provider",
            path: "/svc_for_sys/fuchsia.fonts.Provider",
        },
    ],
}
```

### Offer services to your component {#offer-services}

To work properly, your component must be offered all services that appear in
its [`use`][manifests-use] declarations. These services may be provided by
v1 or v2 components. Look in the sysmgr config files and `core.cml` to find the
originating components ([example search][sysmgr-config-search]).

There are three possible cases:

-   [v1 component provides service](#v1-component-provides-service):
    The provider of the service is a v1 component.
-   [v2 component in `core.cml` provides service](#v2-core-cml-provides-service):
    The provider of the service is a v2 component that's a child of `core.cml`.
-   The provider of the service is a v2 component that's not child of `core.cml`.
    If this is the case, reach out to [component-framework-dev][cf-dev-list] for
    assistance.

Note: You must also route all services requested by any manifest shards listed
in your manifest's [`include`][manifests-include].

#### v1 component provides service {#v1-component-provides-service}

You’ll reach this case if a mapping for the service exists in a sysmgr config
file. Take a look at [`appmgr.cml`][cs-appmgr-cml], and search for the service.
If it’s already exposed, no modifications are required. If not, you’ll need to
change `appmgr.cml` to expose the service and route it from `appmgr` to your
component:

```json5
// appmgr.cml
{
    expose: [
        ...
        {
            protocol: [
                ... // (Any services already exposed from appmgr are here)
                "fuchsia.pkg.FontResolver",
            ],
            from: "self",
        },
        ...
    ],
}
```

```json5
// core.cml
{
    offer: [
        ...
        {
            protocol: "fuchsia.logger.LogSink",
            from: "parent",
            to: [ "#font_provider" ],
        },
        {
            protocol: [
                "fuchsia.pkg.FontResolver",
            ],
            from: "#appmgr",
            to: [ "#font_provider" ],
        },
        ...
    ],
}
```

#### v2 component in core.cml provides service {#v2-core-cml-provides-service}

Route the service from the component in `core` that exposes it to your component
in `core.cml`:

```json5
// core.cml
{
    offer: [
        ...
        {
            protocol: [ "fuchsia.pkg.FontResolver" ],
            from: "#font_resolver",
            to: [ "#font_provider" ],
        },
        ...
    ],
}
```

### Remove sysmgr configuration entries {#remove-config-entries}

Before you test your component, remove the service mappings in
[`services.config`][example-services-config] and other sysmgr configuration
files you identified previously.

Without this step, sysmgr will report errors attempting to load services from
your v1 component instead of using the new capabilities routed to it through
`core.cml`.

```json5
// services.config
{
    "services": {
        ...
        // Delete these lines
        "fuchsia.fonts.Provider": "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx",
        ...
    }
}
```

### Test your component {#test-component}

It is recommended that you manually verify that your component and its
dependencies still work. Perform manual verification of capability routing as it
is usually outside the scope of hermetic tests.

If your component manifest contains additional system features that haven't been
migrated at this point, see [Other common capabilities](#other-capabilities)
and [Converting CMX features](#cmx-features) for additional guidance.

If your component or one of the components that depends on it isn't working
correctly, try following the advice in
[Troubleshooting components][troubleshooting-components].

Once your component has been registered in the v2 topology and all tests
have been converted, you can delete the Components v1 definition of your
component. Find and remove any CMX files for your component and its tests,
including any remaining references to it from the package rule(s) you modified
when you [migrated the component manifest](#create-component-manifest).


## Other common capabilities {#other-capabilities}

This section provides guidance on migrating other capabilities that are common
to most components.

### Inspect

If your component is using inspect, you'll need to expose [Inspect][inspect]
information to the framework. Inspect data is accessible under the
`/diagnostics` directory in the component outgoing directory. A v2 component
has to explicitly expose this directory to the framework. This allows
inspect data to be readable by the [Archivist][archivist] for snapshots,
[iquery][iquery], etc.

You can add this to your component by including the following manifest shard:

```json5
// fonts.cml
{
    // Expose the diagnostics directory capability for Inspect
    include: [ "sdk/lib/diagnostics/inspect/client.shard.cml" ],
    ...
}
```

### Resolvers

If your component is not part of the `base` package set you must route the
`universe` resolver to it. You can check if your package is in the `base` set.

```posix-terminal
fx list-packages --base
```

Note: You can use `--cache` and `--universe` to inspect what is in the `cache`
and `universe` sets, respectively.

Resolvers are routed to components via environments. First define the
environment in the `environments` section.

```json5
// core.cml
{
  environments: [
    ...
    {
      name: "my_component_env",
      // inherits things from your parent's realm, almost always the right thing
      extends: "realm",
      resolvers: [
        {
          resolver: "universe-resolver",
          scheme: "fuchsia-pkg",
          // "#universe-resolver" is a child of core
          from: "#universe-resolver",
        },
      ],
    },
  ]
}
```

Then assign the `environment` to your component.

```json5
// core.cml
{
  children: [
    ...
    {
      name: "my_component",
      url: "fuchsia-pkg://fuchsia.com/my-pkg#meta/my_component.cm",
      environment: "#my_component_env",
    },
  ],
}
```

### Logging to stdout/stderr {#logging-to-stdout-stderr}

In components v1 appmgr redirected `stderr` and `stdout` to the debug log, but
in v2 these outputs are not redirected anywhere. If your component writes log
data to the debug log you'll need to make the changes described in this section.

To write to the debug log your component needs access to the
`fuchsia.boot.WriteOnlyLog` capability. This capability is offered to `core` and
can be offered to your component.

```json5
{
  offer: [
  ...
    {
      protocol: [ "fuchsia.boot.WriteOnlyLog" ],
      from: "parent",
      to: [ "#my_component" ],
    },
  ],
}
```

Next, in your program you must direct `stderr` and `stdout` to the debug log.
You can use libraries for the initialization if your component is written in
[Rust][debug-log-rust] or [C++][debug-log-cpp].

Note: If the component isn't  written in C++ or Rust you can use the existing
libraries as a template for how to perform the initialization.

## Converting CMX features {:#cmx-features}

This section provides guidance on migrating CMX [`features`][cmx-services].
If there's a feature in your CMX file that's not in this list, please reach out
to [component-framework-dev][cf-dev-list].

### Storage features {#storage-features}

If your component uses any of the following features, follow the instructions in
this section to migrate storage access:

| Feature | Description | Storage Capability | Path |
| ------- | ----------- | ------------------ | ---- |
| `isolated-persistent-storage` | Isolated persistent storage directory | `data` | `/data` |
| `isolated-cache-storage` | Managed persistent storage directory | `cache` | `/cache` |
| `isolated-temp` | Managed in-memory storage directory | `temp` | `/tmp` |

These features are supported in v2 components using
[storage capabilities][storage-capabilities].

#### Declare the required storage capabilities

When [migrating your component manifest](#create-component-manifest), add
the following to your CML file:

```json5
// my_component.cml
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

#### Inject storage capabilities into tests

When [migrating tests](#migrate-tests), you will need to inject storage access
into your test component if the test driver or any of the other components in
the test realm access a storage path.

Following the example in [Test uses injected services](#injected-services), add
the following to route storage access to your test driver from the test root:

```json5
// test_root.cml
}
    children: [
        {
            name: "test_driver",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component_test.cm",
        },
    ],
    offer: [
        ...
        {
            storage: "{{ '<var label="storage">data</var>' }}",
            from: "parent",
            to: [ "#test_driver" ],
        },
    ],
}
```

#### Route storage from the parent realm

When [adding your component](#add-component-to-topology),
you'll need to offer the appropriate storage path to your component from its
parent realm.

```json5
// core.cml
{
    children: [
        ...
        {
            name: "my_component",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component.cm",
        },
    ],
    offer: [
        ...
        {
            storage: "{{ '<var label="storage">data</var>' }}",
            from: "self",
            to: [ "#my_component" ],
        },
    ]
}
```

Note: If the appropriate storage capability is not currently provided by your
component's parent realm, reach out to [component-framework-dev][cf-dev-list]
for assistance.

#### Update component storage index

Components that use storage use a [component ID index][component-id-index] to
preserve access to persistent storage contents across the migration, such as
[`core_component_id_index.json5`][example-component-id-index].
You must update the component index to map the new component moniker to the same
instance within the component that provides the storage capability.

Find any instances of your current v1 component in component index files:

```json5
// core_component_id_index.json5
{
    instances: [
        ...
        {
            instance_id: "...",
            appmgr_moniker: {
                url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component.cmx",
                realm_path: [ ... ]
            }
        }
    ]
}
```

Replace the `appmgr_moniker` for your component instance with the new moniker in
the migrated v2 realm, keeping the same `instance_id`:

```json5
// core_component_id_index.json5
{
    instances: [
        ...
        {
            instance_id: "...",
            moniker: "/core/my_component"
        }
    ]
}
```

Note: If you are migrating your component to a realm other than `core`, the
moniker should reflect that.

### Directory features {#directory-features}

If your component uses any of the following features, follow the instructions in
this section to migrate directory access:

| Feature | Description | Directory Capability | Path |
| ------- | ----------- | -------------------- | ---- |
| `factory-data` | Read-only factory partition data | `factory` |`/factory` |
| `durable-data` | Persistent data that survives factory reset | `durable` |`/durable` |
| `shell-commands` | Executable directory of shell binaries | `bin` | `/bin` |
| `root-ssl-certificates` | Read-only root certificate data | `config-ssl` | `/config/ssl` |

These features are supported in v2 components using
[directory capabilities][directory-capabilities].

#### Declare the required directory capabilities

When [migrating your component manifest](#create-component-manifest), add
the following to your CML file:

```json5
// my_component.cml
{
  use: [
      ...
      {
          directory: "{{ '<var label="directory">config-ssl</var>' }}",
          rights: [ "r*" ],
          path: "{{ '<var label="directory path">/config/ssl</var>' }}",
      },
}
```

Note: Unlike storage locations, which are isolated per-component, directories
are a shared resource. You may need to also determine the **subdirectory** your
component needs to access in order to complete this migration.

#### Inject directory path into tests

When [migrating tests](#migrate-tests), you need to inject the directory
capabilities in your test if the test driver or any of the other components
in the test realm require directory access.

Following the example in [Test uses injected services](#injected-services), add
the following to route directory access to your test driver from the test root:

```json5
// test_root.cml
{
    children: [
        {
            name: "test_driver",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component_test.cm",
        },
    ],
    offer: [
        ...
        {
            directory: "{{ '<var label="directory">config-ssl</var>' }}",
            from: "parent",
            to: [ "#test_driver" ],
        },
    ],
}
```

#### Route directory from the parent realm

When [adding your component](#add-component-to-topology), you'll need to offer
the directory capabilities to your component.

```json5
// core.cml
{
    children: [
        ...
        {
            name: "my_component",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component.cm",
        },
    ],
    offer: [
        {
            directory: "{{ '<var label="directory">config-ssl</var>' }}",
            from: "parent",
            to: [ "#my_component" ],
        },
        ...
    ],
}
```

Note: If the appropriate directory capability is not currently provided by your
component's parent realm, reach out to [component-framework-dev][cf-dev-list]
for assistance.

### Configuration data {#config-data}

If your component uses any of the following features, follow the instructions in
this section to migrate directory access:

| Feature | Description | Directory Capability | Path |
| ------- | ----------- | -------------------- | ---- |
| `config-data` | Read-only configuration data | `config-data` |`/config/data` |

These features are supported in v2 components using
[directory capabilities][directory-capabilities].

#### Declare the required directory capabilities

When [migrating your component manifest](#create-component-manifest), add
the following to your CML file:

```json5
// my_component.cml
{
  use: [
      ...
      {
          directory: "config-data",
          rights: [ "r*" ],
          path: "/config/data",
      },
}
```

#### Inject directory path into tests

When [migrating tests](#migrate-tests), you need to inject the directory
capability with the appropriate subdirectory in your test if the test driver or
any of the other components in the test realm require directory access.
The name of the subdirectory should match the name of the package that contains
the component.

Following the example in [Test uses injected services](#injected-services), add
the following to route directory access to your test driver from the test root:

```json5
// test_root.cml
{
    children: [
        {
            name: "test_driver",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component_test.cm",
        },
    ],
    offer: [
        ...
        {
            directory: "config-data",
            from: "parent",
            to: [ "#test_driver" ],
            subdir: "{{ '<var label="package name">my-package</var>' }}",
        },
    ],
}
```

#### Route directory from the parent realm

When [adding your component](#add-component-to-topology), you'll need to offer
the directory capability with the appropriate subdirectory to your component.

```json5
// core.cml
{
    children: [
        ...
        {
            name: "my_component",
            url: "fuchsia-pkg://fuchsia.com/my-package#meta/my_component.cm",
        },
    ],
    offer: [
        {
            directory: "config-data",
            from: "parent",
            to: [ "#my_component" ],
            subdir: "{{ '<var label="package name">my-package</var>' }}",
        },
        ...
    ],
}
```

[archivist]: /docs/reference/diagnostics/inspect/tree.md#archivist
[build-migration]: /docs/development/components/build.md#legacy-package-migration
[cf-dev-list]: https://groups.google.com/a/fuchsia.dev/g/component-framework-dev
[cmx-services]: /docs/concepts/components/v1/component_manifests.md#sandbox
[code-search]: https://cs.opensource.google/fuchsia
[component-id-index]: /docs/development/components/component_id_index.md
[components-intro]: /docs/concepts/components/v2/introduction.md
[components-topology]: /docs/concepts/components/v2/topology.md
[components-migration-status]: /docs/concepts/components/v2/migration.md
[cs-appmgr-cml]: /src/sys/appmgr/meta/appmgr.cml
[cs-core-cml]: /src/sys/core/meta/core.cml
[debug-log-cpp]: /src/sys/lib/stdout-to-debuglog/cpp
[debug-log-rust]: /src/sys/lib/stdout-to-debuglog/rust
[directory-capabilities]: /docs/concepts/components/v2/capabilities/directory.md
[example-component-id-index]: /src/sys/appmgr/config/core_component_id_index.json5
[example-fonts]: https://fuchsia.googlesource.com/fuchsia/+/cd29e692c5bfdb0979161e52572f847069e10e2f/src/fonts/meta/fonts.cmx
[example-package-rule]: https://fuchsia.googlesource.com/fuchsia/+/cd29e692c5bfdb0979161e52572f847069e10e2f/src/fonts/BUILD.gn
[example-services-config]: /src/sys/sysmgr/config/services.config
[ftf-intro]: /docs/concepts/testing/v2/test_runner_framework.md
[ftf-roles]: /docs/concepts/testing/v2/test_runner_framework.md#test-roles
[ftf-test-runners]: /docs/concepts/testing/v2/test_runner_framework.md#test-runners
[ftf-provided-test-runners]: /src/sys/test_runners
[ftf-test-suite]: /docs/concepts/testing/v2/test_runner_framework.md#test-suite-protocol
[fuchsia-test-facets]: /docs/concepts/testing/v1_test_component.md
[glossary-component-manifest]: /docs/glossary.md#component-manifest
[glossary-components-v1]: /docs/glossary.md#components-v1
[glossary-components-v2]: /docs/glossary.md#components-v2
[glossary-environment]: /docs/glossary.md#environment
[glossary-runner]: /docs/glossary.md#runner
[inspect]: /docs/development/diagnostics/inspect/README.md
[iquery]: /docs/reference/diagnostics/consumers/iquery.md
[manifests-capabilities]: /docs/concepts/components/v2/component_manifests.md#capabilities
[manifests-expose]: /docs/concepts/components/v2/component_manifests.md#expose
[manifests-program]: /docs/concepts/components/v2/component_manifests.md#program
[manifests-use]: /docs/concepts/components/v2/component_manifests.md#use
[manifests-include]: /docs/concepts/components/v2/component_manifests.md#include
[storage-capabilities]: /docs/concepts/components/v2/capabilities/storage.md
[sysmgr-config-search]: https://cs.opensource.google/search?q=fuchsia-pkg:%2F%2Ffuchsia.com%2F.*%23meta%2Fmy_component.cmx%20-f:.*.cmx$%20%5C%22services%5C%22&ss=fuchsia
[sysmgr-config]: /src/sys/sysmgr/sysmgr-configuration.md
[system-services]: /docs/concepts/testing/v1_test_component.md#services
[troubleshooting-components]: /docs/development/components/v2/troubleshooting.md
