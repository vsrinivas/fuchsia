# Realm Builder

The Realm Builder library exists to facilitate integration testing of
components by allowing for the run-time construction of [realms][realms] and
mocked components specific to individual test cases.

If a test wishes to launch a child component, then Realm Builder is likely a
good fit for assisting the test.

If a test does not benefit from having either realms tailor made to each test
case or realms containing mocked components unique to each test case, then the
test can likely be made simpler to implement, understand, and maintain by using
static component manifests. If a test does call for either (or both) of these
things, then Realm Builder is a good fit for assisting the test.

The Realm Builder library is available in multiple languages, and the exact
semantics and abilities available in each language may vary. For a comprehensive
list of features and supported languages, see the
[feature matrix at the end of this document](#language-feature-matrix).

## Add Dependencies {#add-deps}

The Realm Builder client libraries rely on special capabilities to work. Therefore,
tests using this library must include the necessary shard in their
test component's manifest:

```json5
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/meta/sample_realm.cml" region_tag="include_shard_rust" adjust_indentation="auto" %}
```

Afterwards, you should add the GN dependency for your test's language:

* {Rust}

    **Add the Rust Realm Builder library to your `BUILD.gn` file**

    ```gn
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/BUILD.gn" region_tag="realm_builder_dep_rust" adjust_indentation="auto" %}
    ```

* {C++}

    **Add the C++ Realm Builder library to your `BUILD.gn` file**

    ```gn
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/BUILD.gn" region_tag="realm_builder_dep_cpp" adjust_indentation="auto" %}
    ```

## Initialize Realm Builder {#init-realm}

After adding the necessary dependencies, initialize Realm Builder inside your
test component.

* {Rust}

    This section assumes that you are writing an asynchronous test and that
    some part of your component looks similar to this:

    ```rust
    #[fuchsia::test]
    async fn test() -> Result<(), Error> {
        // ...
    }
    ```

    **Import Realm Builder library**

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="import_statement_rust" adjust_indentation="auto" %}
    ```

    **Initialize `RealmBuilder` struct**

    Create a new `RealmBuilder` instance for each test case in your test.
    This creates a unique, isolated, child realm that ensures that the side-effects
    of one test case do not affect the others.

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="init_realm_builder_rust" adjust_indentation="auto" %}
    ```

* {C++}

    This section assumes that you are writing an asynchronous test and that
    your testing is executing inside a message loop. Typically, such cases look
    like this:

    ```cpp
    #include <lib/async-loop/cpp/loop.h>
    #include <lib/async-loop/cpp/default.h>

    TEST(SampleTest, CallEcho) {
        async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
        // Test code below
    }
    ```

    **Import Realm Builder library**

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="import_statement_cpp" adjust_indentation="auto" %}
    ```

    **Use library namespace**
    This step is optional. It imports the entire library's namespace,
    for convenience when writing and reading tests.

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="use_namespace_cpp" adjust_indentation="auto" %}
    ```

    **Initialize `Realm::Builder` class**

    Create a new `Realm::Builder` instance for each test case in your test.
    This creates a unique, isolated, child realm that ensures that the side-effects
    of one test case do not affect the others.

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="init_realm_builder_cpp" adjust_indentation="auto" %}
    ```

## Construct Realm {#construct-realm}

With the constructed Realm Builder object for your target, you can now begin
assembling the realm.

Use the Realm Builder instance to add child components to the realm with the
language's add component function. Each child component requires the following:

1.  **Component name:** Unique identifier for the component inside the realm.
    For static components, this maps to the `name` attribute of an instance
    listed in the [`children`][children] section of the component manifest.
1.  **Component source:** Defines how the component is created when the realm is
    built. For static components, this should be a `URL` with a
    valid [component URL][component-urls]. This maps to the `url` attribute of
    an instance listed in the [`children`][children] section of a component
    manifest.

The example below adds two static child components to the created realm:

*   Component `a` loads from `fuchsia-pkg://fuchsia.com/foo#meta/foo.cm`
*   Component `b` loads from `#meta/bar.cm`

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="add_component_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="add_component_cpp" adjust_indentation="auto" %}
    ```

Note: Realm Builder interprets component sources defined using a relative URL
to be contained in the same package as the test component.

### Adding Legacy Components {#legacy-components}

Realm Builder also supports adding Legacy Components to your realm:

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="add_legacy_component_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="add_legacy_component_cpp" adjust_indentation="auto" %}
    ```

### Adding Mock Components {#mock-components}

Mock components allow tests to supply a local function that behaves as a
dedicated component. Realm Builder implements the protocols that enables the
component framework to treat the local function as a component and handle
incoming FIDL connections. The local function can hold state specific to the
test case where it is used, allowing each constructed realm to have a mock for
its specific use case.

The following example demonstrates a mock component that implements the
`fidl.examples.routing.echo.Echo` protocol.

First, you must implement your mock component.

* {Rust}

    In Rust, a mock component is implemented via function that has the following
    signature:

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/src/mock.rs" region_tag="mock_interface_rust" adjust_indentation="auto" %}
    ```

    `MockHandles` is a struct containing handles to the component's incoming
    and outgoing capabilities:

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/src/mock.rs" region_tag="mock_handles_rust" adjust_indentation="auto" %}
    ```

    An implementation for a mock component would look like:

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="mock_component_impl_rust" adjust_indentation="auto" %}
    ```

* {C++}

    In C++, a mock component is implemented by creating a class that inherits from
    LocalComponent interface and overrides the `Start` method.

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="sdk/lib/sys/component/cpp/testing/realm_builder_types.h" region_tag="mock_interface_cpp" adjust_indentation="auto" %}
    ```

    `LocalComponentHandles` is a class containing handles to the component's incoming
    and outgoing capabilities:

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="sdk/lib/sys/component/cpp/testing/realm_builder_types.h" region_tag="mock_handles_cpp" adjust_indentation="auto" %}
    ```

    An implementation for a mock component would look like:

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="mock_component_impl_cpp" adjust_indentation="auto" %}
    ```

After your mock implementation is complete, you may add it your realm:

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="add_mock_component_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="add_mock_component_cpp" adjust_indentation="auto" %}
    ```

## Route Capabilities {#routing}

By default there are no [capability routes][cap-routes] in the created realm.
To route capabilities to components using Realm Builder, call the add route
function with the appropriate capability route.

### Routing between child components {#routing-between-children}

The following example adds a capability route to [offer][offer] components `a`,
`b`, and `c` the `fidl.examples.routing.echo.Echo` protocol from component `d`.

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="route_between_children_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="route_between_children_cpp" adjust_indentation="auto" %}
    ```

### Exposing realm capabilities {#routing-from-realm}

To route capabilities provided from inside the created realm to the test component,
set the target of the capability route using above root endpoint.
The created realm automatically [`exposes`][expose] the capability to its
parent. This allows the Realm Builder instance to access the exposed capability.

The following example exposes a `fidl.examples.routing.echo.Echo` protocol to
the parent test component:

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="route_to_test_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="route_to_test_cpp" adjust_indentation="auto" %}
    ```

### Offering test capabilities {#routing-from-test}

To route capabilities from the test component to components inside the created
realm, set the source of the capability route using above root endpoint.
Consider the following example to make the `fuchsia.logger.LogSink` protocol from
the parent's realm available all the child components of the Realm.

The `fuchsia.logger.LogSink` protocol is offered by default to the created realm
through the [Realm Builder shard][realm-builder-shard].

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="route_from_test_rust" adjust_indentation="auto" %}
    ```


* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="route_to_test_cpp" adjust_indentation="auto" %}
    ```

This shard offers the following capabilities to the test component:

```json5
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="sdk/lib/sys/component/realm_builder_base.shard.cml" region_tag="collection_offers" adjust_indentation="auto" %}
```

To route a capability that isn't in the Realm Builder shard,
[offer][offer] it directly:

```json5
{
    include: [
        "sys/component/realm_builder.shard.cml",
    ],
    children: [
        {
            name: "some-child",
            url: "...",
        },
    ],
    offer: [
        {
            protocol: "fuchsia.example.Foo",
            from: "#some-child",
            to: [ "#fuchsia_component_test_collection" ],
        },
    ],
    ...
}
```

Once routed to the parent component, it can be offered to child components
in the test realm:

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="route_from_test_sibling_rust" adjust_indentation="auto" %}
    ```


* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="route_from_test_sibling_cpp" adjust_indentation="auto" %}
    ```

## Creating the realm {#create-realm}

After you have added all the components and routes needed for the test case,
use build method to create the realm and make its components ready to
execute.

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="build_realm_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="build_realm_cpp" adjust_indentation="auto" %}
    ```

Note: The constructed realm instance is immutable. You cannot change components
or routes after calling the build method.

Use the realm returned by the build method to perform additional tasks.
Any eager components in the realm execute immediately, and any
capabilities routed using the above root endpoint are now accessible by the test.

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="call_echo_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="call_echo_cpp" adjust_indentation="auto" %}
    ```

When the realm object goes out of scope, Component Manager destroys the realm
and its children.

## Advanced Configuration {#advanced}

### Modifying generated manifests (Rust only) {#modifying-manifests}

For cases where the capability routing features supported by the add route
method are not sufficient, you can manually adjust the manifest declarations.
Realm Builder supports this for the following component types:

*   Mock components created by Realm Builder.
*   URL components contained in the same package as the test component.

After [constructing the realm](#construct-realm):

1.  Use the get decl method of the constructed realm to obtain a specific
    child's manifest.
1.  Modify the appropriate manifest attributes.
1.  Substitute the updated manifest for the component by calling the
    set decl method.

* {Rust}

    ```rust
    let mut root_manifest = builder.get_decl(Moniker::root()).await?;
    // root_manifest is mutated in whatever way is needed
    builder.set_decl(Moniker::root(), root_manifest).await?;

    let mut a_manifest = builder.get_decl("a").await?;
    // a_manifest is mutated in whatever way is needed
    builder.set_decl("a", a_manifest).await?;
    ```

When [adding routes](#routing) for modified components, add them directly to
the **constructed realm** where you obtained the manifest instead of using the
builder instance. This ensures the routes are properly validated against the
modified component when the [realm is created](#create-realm).

### Determining a moniker {#test-component-moniker}

The moniker for a Realm Builder child component looks like the following:

```none
fuchsia_component_test_collection:{{ '<var>' }}child-name{{ '</var>' }}/{{ '<var>' }}component-name{{ '</var>' }}
```

The moniker consists of the following elements:

*   `child-name`: An auto-generated name for the realm's collection, created
    by the Realm Builder library. Obtained by calling the `child_name()`
    function of the constructed realm.
*   `component-name`: The "Component name" parameter provided to the
    `Add Component` component when [constructing the realm](#construct-realm).


To obtain the child name invoke the following method on the constructed Realm:

* {Rust}

    ```rust
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/rust/src/lib.rs" region_tag="get_child_name_rust" adjust_indentation="auto" %}
    ```

* {C++}

    ```cpp
    {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/realm_builder/cpp/sample.cc" region_tag="get_child_name_cpp" adjust_indentation="auto" %}
    ```

## Troubleshooting {#troubleshoot}

### Invalid capability routes {#invalid-routes}

The add route function cannot validate if a capability is properly offered
to the created realm from the test component.

If you attempt to route capabilities with a source of above root without a
corresponding [offer][offer], requests to open the capability will not resolve
and you will see error messages similar to the following:

```
[86842.196][klog][E] [component_manager] ERROR: Failed to route protocol `fidl.examples.routing.echo.Echo` with target component `/core:0/test_manager:0/tests:auto-10238282593681900609:4/test_wrapper:0/test_root:0/fuchsia_component_test_
[86842.197][klog][I] collection:auto-4046836611955440668:16/echo-client:0`: An `offer from parent` declaration was found at `/core:0/test_manager:0/tests:auto-10238282593681900609:4/test_wrapper:0/test_root:0/fuchsia_component_test_colle
[86842.197][klog][I] ction:auto-4046836611955440668:16` for `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the parent
```

For more information on how to properly offer capabilities from the test
controller, see [offering external capabilities](#routes-from-outside).

## Language feature matrix {#language-feature-matrix}

|                             | Rust | C++  |
| --------------------------- |:----:|:----:|
| Legacy components           |    Y |    Y |
| Mock components             |    Y |    Y |
| Manipulating component decl |    Y |    N |


[cap-routes]: /docs/concepts/components/v2/capabilities/README.md#routing
[children]: https://fuchsia.dev/reference/cml#children
[collection]: https://fuchsia.dev/reference/cml#collections
[component-urls]: /docs/concepts/components/component_urls.md
[environment]: https://fuchsia.dev/reference/cml#environments
[expose]: https://fuchsia.dev/reference/cml#expose
[namespaces]: /docs/concepts/process/namespaces.md
[offer]: https://fuchsia.dev/reference/cml#offer
[realm-builder-shard]: /sdk/lib/sys/component/realm_builder_base.shard.cml
[realms]: /docs/concepts/components/v2/realms.md
[resolver]: /docs/concepts/components/v2/capabilities/resolvers.md
[runner]: /docs/concepts/components/v2/capabilities/runners.md
[shard-includes]: https://fuchsia.dev/reference/cml#include
[test-runner]: /docs/development/testing/components/test_runner_framework.md#test-runners
[use]: https://fuchsia.dev/reference/cml#use
