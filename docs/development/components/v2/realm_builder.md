# Realm builder

The realm builder library exists to facilitate integration testing of
components by allowing for the run-time construction of [realms][realms] and
mocked components specific to individual test cases.

If a test wishes to launch a child component, then realm builder is likely a
good fit for assisting the test.

If a test does not benefit from having either realms tailor made to each test
case or realms containing mocked components unique to each test case, then the
test can likely be made simpler to implement, understand, and maintain by using
static component manifests. If a test does call for either (or both) of these
things, then realm builder is a good fit for assisting the test.

The realm builder library is available in multiple languages, and the exact
semantics and abilities available in each language may vary. This document uses
Rust in its example code, but the concepts shown exist in all versions of the
library. For a comprehensive list of features and which languages they are
supported in, see the [feature matrix at the end of this
document](#language-feature-matrix).

## Set up realm builder {#set-up}

Add the [realm builder CML shard][realm-builder-shard] as an [`include`][shard-includes]
in the test's manifest:

```json5
{
    include: [
        "//src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml",
    ],
    ...
}
```

This shard declares a [component collection][collection] called
`fuchsia_component_test_collection` where the constructed realms run,
and [offers][offer] a set of default capabilities to those realms:

```json5
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml" region_tag="collection_offers" adjust_indentation="auto" %}
```

## Construct the component topology {#construct-realm}

Create a new `RealmBuilder` instance for each test case in your test.
This creates a unique, isolated, child realm that ensures that the side-effects
of one test case do not affect the others.

Use the `RealmBuilder` instance to add child components to the realm with the
`add_component()` function. Each child component requires the following:

1.  **Component name:** Unique identifier for the component inside the realm.
    For static components, this maps to the `name` attribute of an instance
    listed in the [`children`][children] section of the component manifest.
1.  **Component source:** Defines how the component is created when the realm is
    built. For static components, this should be a `ComponentSource::url` with a
    valid [component URL][component-urls]. This maps to the `url` attribute of
    an instance listed in the [`children`][children] section of a component
    manifest.

The example below adds two static child components to the created realm:

*   Component `a` loads from `fuchsia-pkg://fuchsia.com/foo#meta/foo.cm`
*   Component `b` loads from `fuchsia-pkg://fuchsia.com/bar#meta/bar.cm`

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="add_a_and_b_example" adjust_indentation="auto" %}
```

This creates the following component topology:

```none
   <root>
  /      \
 a        b
```

Note: Realm builder interprets component sources defined using a relative URL
to be contained in the same package as the test controller.

### Adding a mock component {#mock-components}

Mock components allow tests to supply a local function that behaves as a
dedicated component.
Realm builder implements the protocols that enables the component framework to
treat the local function as a component and handle incoming FIDL connections.
The local function can hold state specific to the test case where it is used,
allowing each constructed realm to have a mock for its specific use case.

The following example serves the `fidl.examples.routing.echo.Echo` protocol
from a mock component that handles protocol requests from a local
`echo_server_mock()` function:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="mock_component_example" adjust_indentation="auto" %}
```

The `echo_server_mock()` creates a new `ServiceFs` to handle incoming FIDL
connections. When the `echo_string()` protocol function is called, the mock
sends a message to the test controller.

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="echo_server_mock" adjust_indentation="auto" %}
```

## Add capability routes {#add-routes}

By default there are no [capability routes][cap-routes] in the created realm.
To route capabilities to components using `RealmBuilder`, call the `add_route()`
function with the appropriate `CapabilityRoute`.

The following example adds a `CapabilityRoute` to [offer][offer] component `b`
the `fidl.examples.routing.echo.Echo` protocol from component `a`.

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_from_a_to_b_example" adjust_indentation="auto" %}
```

### Exposing realm capabilities {#routes-to-test}

To route capabilities provided from inside the created realm to the test controller,
set the target of the `CapabilityRoute` using `RouteEndpoint::above_root()`.
The created realm will automatically [`expose`][expose] the capability to its
parent. This allows the `RealmBuilder` instance to access the exposed capability.

The following example exposes a `fidl.examples.routing.echo.EchoClientStats`
protocol to the parent test component:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_to_above_root_example" adjust_indentation="auto" %}
```

### Offering external capabilities {#routes-from-outside}

To route capabilities from the test controller to components inside the created
realm, set the source of the `CapabilityRoute` using `RouteEndpoint::above_root()`.
Consider the following example to make the `fuchsia.logger.LogSink` protocol from
the parent's realm available to components `a` and `b`:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_logsink_example" adjust_indentation="auto" %}
```

The `fuchsia.logger.LogSink` protocol is offered by default to the created realm
through the [realm builder shard][realm-builder-shard].
To route a capability that isn't in the realm builder shard,
[offer][offer] it directly:

```json5
{
    include: [
        "//src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml",
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

## Creating the realm {#create-realm}

After you have added all the components and routes needed for the test case,
use `realm.create()` to create the realm and make its components ready to
execute.

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="create_realm" adjust_indentation="auto" %}
```

Note: The constructed realm instance is immutable. You cannot change components
or routes after calling `create()`.

Use the `realm_instance` returned by `create()` to perform additional tasks.
Any eager components in the realm will immediately execute, and any
capabilities routed using `above_root` are now accessible by the test.

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="connect_to_protocol" adjust_indentation="auto" %}
```

## Destroying the realm {#destroy-realm}

When the test no longer needs the realm, it can be destroyed by destroying the
realm instance returned by `create()`:

```rust
// As per rust semantics, this also happens when `realm_instance` goes out of
// scope.
drop(realm_instance);
```

This action instructs Component Manager to destroy the realm and all its
children.

## Advanced configuration

### Modifying generated manifests

For cases where the capability routing features supported by `add_route()` are
not sufficient, you can manually adjust the manifest declarations. Realm builder
supports this for the following component types:

*   Mock components created by realm builder.
*   URL components contained in the same package as the test controller.

After [constructing the realm](#construct-realm):

1.  Use the `get_decl()` function of the constructed realm to obtain a specific
    child's manifest.
1.  Modify the appropriate manifest attributes.
1.  Substitute the updated manifest for the component by calling the
    `set_decl()` function.

See the following example:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="mutate_generated_manifest_example" adjust_indentation="auto" %}
```

When [adding routes](#add-routes) for modified components, add them directly to
the **constructed realm** where you obtained the manifest instead of using the
builder instance. This ensures the routes are properly validated against the
modified component when the [realm is created](#create-realm).

### Determining a moniker {#test-component-moniker}

The moniker for a `RealmBuilder` child component looks like the following:

```none
fuchsia_component_test_collection:{{ '<var>' }}child-name{{ '</var>' }}/{{ '<var>' }}component-name{{ '</var>' }}
```

The moniker consists of the following elements:

*   `child-name`: Obtained by calling the `child_name()` function of the constructed realm.
*   `component-name`: The "Component name" parameter provided to `add_component()` when
    [constructing the realm](#construct-realm).

## Troubleshooting

### Invalid capability routes

The `add_route()` function cannot validate if a capability is properly offered
to the created realm from the test controller.

If you attempt to route capabilities with a source of `above_root` without a
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

|                          | Rust |
| ------------------------ |:----:|
| Legacy components        |    N |
| Mock components          |    Y |
| Strong capability routes |    Y |
| Weak capability routes   |    N |
| Custom environments      |    N |
| Setting subdirectories   |    N |


[cap-routes]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[children]: /docs/concepts/components/v2/component_manifests.md#children
[collection]: /docs/concepts/components/v2/component_manifests.md#collections
[component-urls]: /docs/concepts/components/component_urls.md
[environment]: /docs/concepts/components/v2/component_manifests.md#environments
[expose]: /docs/concepts/components/v2/component_manifests.md#expose
[namespaces]: /docs/concepts/process/namespaces.md
[offer]: /docs/concepts/components/v2/component_manifests.md#offer
[realm-builder-shard]: /src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml
[realms]: /docs/concepts/components/v2/realms.md
[resolver]: /docs/concepts/components/v2/capabilities/resolvers.md
[runner]: /docs/concepts/components/v2/capabilities/runners.md
[shard-includes]: /docs/concepts/components/v2/component_manifests.md#include
[test-runner]: /docs/concepts/testing/v2/test_runner_framework.md#test-runners
[use]: /docs/concepts/components/v2/component_manifests.md#use
