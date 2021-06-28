# Realm builder

The realm builder library exists to facilitate integration testing of v2
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

## Setting up realm builder

When the realm builder library is to be used in a test, the [realm builder CML
shard][realm-builder-shard] must be [included][shard-includes] in the test's
manifest. The shard brings important configuration needed to set up the
collection in which the constructed realms will run. The library will not work
without it.

```
{
    include: [
        "//src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml",
    ],
    ...
}
```

## Constructing a realm

The best practice is to create a new `RealmBuilder` instance (by calling the
library constructor) for each test case in your test. This will create a unique,
isolated, child realm that ensures that the side-effects of one test case do not
affect the others.

Within this realm, you can create any number of child components, route
capabilities between them, and even add child components that are implemented
directly within the test controller code.

### Adding a static component as a child

Once a new `RealmBuilder` instance has been created, components can be added to
it. Each added component needs two things: a name it will exist under in the
realm (just like it would when [defining static components in a `.cml`
file][children]), along with a source which defines how the component is created
when the realm is built.

For static components, the provided source should be a `ComponentSource::url`.
[Any valid component URL][component-urls] is a suitable here, if it would work
in the [`children`][children] section of a component manifest then it will work
with realm builder.

In the example below two components are added to a new realm with the names `a`
and `b`. Both of these components are children of the realm's root, and once the
realm is created the component framework will attempt to load component `a` from
`fuchsia-pkg://fuchsia.com/foo#meta/foo.cm`, and `b` from
`fuchsia-pkg://fuchsia.com/bar#meta/bar.cm`.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="add_a_and_b_example" adjust_indentation="auto" %}
```

```
   <root>
  /      \
 a        b
```

### Adding capability routes

By default there are no [capability routes][cap-routes] in the new realm,
neither to nor from any components in the realm. Unless some are added, none of
the components in the realm will be able to access any capabilities.

#### Routes between components in the realm

In our example above where we add components `a` and `b` to our realm, by
default neither of them can access anything from each other. If `b` needs to
access a capability from `a`, then we need to add a capability route between the
two components.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_from_a_to_b_example" adjust_indentation="auto" %}
```

Now, component `b` can open `fidl.examples.routing.echo.Echo` in its
[namespace][namespaces] (presuming it has a suitable [use declaration][use]) and
the request will be routed to component `a`. Component `a` must then serve and
[expose][expose] `fidl.examples.routing.echo.Echo`.

#### Routes from the realm to the test controller

There are often capabilities available inside the created realm which the test
would like to access. For example, the test may wish to verify that component
`b` is behaving as expected by making assertions on state exposed by the
protocol `fidl.examples.routing.echo.EchoClientStats`.

These routes can be added by using a route endpoint of "above root". When a
route has a target of `RouteEndpoint::above_root()` the created realm will
automatically [`expose`][expose] the capability to its parent. This allows the
realm builder instance to access the exposed capability.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_to_above_root_example" adjust_indentation="auto" %}
```

#### Routes from outside the realm #{routes-from-outside}

This section shows how capabilities can be routed from the test controller's
realm to within the generated realm. In order to serve a capability from the
test controller directly and inject _it_ into the realm, jump to [mock
components](#mock-components)."

In order for a capability to be routed to components within any realm built by
realm builder, you must:

- [offer][offer] the capability from the test controller's realm to the
  `#fuchsia_component_test_collection` [collection][collection], and
- add a route from `RouteEndpoint::above_root()` to the target component.

Some capabilities are [offered][offer] automatically by virtue of including the
realm builder CML shard. The set of automatically offered capabilities can be
seen by looking at the relevant offer declarations in realm builder's CML shard:

```json5
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/meta/fuchsia_component_test.shard.cml" region_tag="collection_offers" adjust_indentation="auto" %}
```

As an example, to make the`fuchsia.logger.LogSink` protocol from the parent's
realm available to components `a` and `b`, write the following code:

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="route_logsink_example" adjust_indentation="auto" %}
```

To route a capability that isn't in the realm builder shard, you must
[offer][offer] it directly.

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
            protocol: "my.cool.Protocol",
            from: "#some-child",
            to: [ "#fuchsia_component_test_collection" ],
        },
    ],
    ...
}
```

### Adding a mock component {#mock-components}

Mock components allow tests to supply a local function to behave as a dedicated
component, instead of relying on a component implemented elsewhere. The local
function can hold state specific to the test case it is being used in, allowing
each constructed realm to have a mock geared for its specific use case.

Revisiting our above example where we have two components, `a` and `b`, where
`a` is a static component and serves `fidl.examples.routing.echo.Echo` to `b`,
we could alternatively serve `fidl.examples.routing.echo.Echo` from a mock
component directly. This mock component could be set up to pass a message back
to the test when its echo implementation is accessed, allowing the test to
confirm that `b` is using this protocol as expected.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="mock_component_example" adjust_indentation="auto" %}
```

The `echo_server_mock` function used above can be implemented just like any
standalone component would be, by creating a new `ServiceFs` to handle incoming
FIDL connections. When the `echo_string` function on the protocol is exercised,
the oneshot is consumed to send a message back to the test, allowing the test to
proceed to completion.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="echo_server_mock" adjust_indentation="auto" %}
```

The above example is identical to the one detailed in the [static
components](#static-components) section, with one key difference: component `a`
has a manifest constructed by realm builder and when run its implementation is
provided by a local Rust function, instead of by an executable loaded from a
package.

Under the hood, this works because realm builder implements the correct
protocols to enable the Component Framework to treat the local function just
like any other real component. The framework generates valid handles for the
component, and informs the component's runner (realm builder, in this case) when
the component should start and stop running, just like every other component
running on the system.

It is with those framework-generated handles that the function provides
capabilities into the realm or consumes capabilities from it. Running a
`ServiceFs` on the mock's outgoing directory handle to provide a FIDL protocol
is identical from the framework's perspective to other components, most of which
do the same thing in their implementations.

### Modifying generated manifests

The `.add_route` function of realm builder addresses the majority of capability
routing needs, but does not handle some edge cases. Installing custom
environments, or setting the `subdir` field on a directory capability, are both
currently unsupported.

To address use cases such as these, once the components and routes have been
provided, the individual component manifests that realm builder has assembled can
be manually tweaked.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="mutate_generated_manifest_example" adjust_indentation="auto" %}
```

Note that the manifests for components with a source of `url` can not be fetched
or set, as the contents of these components are not directly available to realm
builder.

## Running a realm

Once realm builder contains all the components and routes needed for the test
case, the real realm can be created, and its components made ready to execute.
This is done by calling `realm.create()`.

Under the hood, realm builder generates a series of component manifests, acts as
a [Resolver] to serve the manifests using generated URLs, and then adds the
generated root URL to a [component collection].

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="create_realm" adjust_indentation="auto" %}
```

At this point, the constructed realm is no longer mutable. All further
interactions must be performed using the `realm_instance` returned by
`create()`. Any eager components will immediately begin execution, and any
capabilities that were routed to `above_root` can be accessed by the test.

```
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/lib/fuchsia-component-test/tests/src/lib.rs" region_tag="connect_to_protocol" adjust_indentation="auto" %}
```

## Realm destruction

When the test no longer needs the realm, it can be destroyed by destroying the
realm instance returned by `create()`:

```
// As per rust semantics, this also happens when `realm_instance` goes out of
// scope.
drop(realm_instance);
```

This action instructs Component Manager to destroy the realm and all its
children.

## Advanced configuration of the test realm collection

If a test author wants more control over the test realm's environment, they can
declare a new collection in the test driver's manifest. Any such collection must
be set up appropriately for realm builder, so copying the contents of realm
builder's shard into the test driver manifest (and renaming the environment and
collection) is a good starting point for setting up a new collection. For a
real-world example of this.

# Troubleshooting

## Invalid capability routes

The `.add_route` function of realm builder is powerful, but it's not always able
to ensure that the routes are fully valid.

One common source of invalid capability routes comes from attempting to route
capabilities with a source of `above_root`: Unless the test's manifest has been
modified to [offer][offer] the capabilities to the
`#fuchsia_component_test_collection` [collection][collection], then requests to
open the capability will not resolve. When a component attempts to access the
capability, Component Manager will follow the offers to the root of the created
realm and then error out, due to a missing offer. Users can know when this
happens by watching for log messages from component manager, such as:

```
[86842.196][klog][E] [component_manager] ERROR: Failed to route protocol `fidl.examples.routing.echo.Echo` with target component `/core:0/test_manager:0/tests:auto-10238282593681900609:4/test_wrapper:0/test_root:0/fuchsia_component_test_
[86842.197][klog][I] collection:auto-4046836611955440668:16/echo-client:0`: An `offer from parent` declaration was found at `/core:0/test_manager:0/tests:auto-10238282593681900609:4/test_wrapper:0/test_root:0/fuchsia_component_test_colle
[86842.197][klog][I] ction:auto-4046836611955440668:16` for `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the parent
```

See the [routes coming from outside the realm](#routes-from-outside) section for
more information on how to set up these routes correctly.

# Language feature matrix {#language-feature-matrix}

|                          | Rust |
| ------------------------ |:----:|
| CFv2 components          |    Y |
| CFv1 components          |    N |
| mock components          |    Y |
| strong capability routes |    Y |
| weak capability routes   |    N |
| custom environments      |    N |
| setting subdirectories   |    N |


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
