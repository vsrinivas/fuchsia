# Writing integration tests for graphics, input, and accessibility

## Checklist

1.  Use RealmBuilder to ensure that your tests are running with a new instance
    of component topology with each run. Use gtest::RealLoopFixture for creating
    your test fixture. If you want your tests to be CTS compatible, use
    zxtest::Test instead.
2.  Do not use CML `injected-services` to construct component topology. (It
    shares the same component instances between different test runs.)
3.  Set up `fuchsia.hardware.display.Provider` by
    bundling[`fake-hardware-display-controller-provider-cmv2-component`](https://cs.opensource.google/fuchsia/fuchsia/+/master:src/ui/bin/hardware_display_controller_provider/BUILD.gn;l=29)
    with the test package.
4.  Create a CFv2 wrapper component for `Root Presenter` around its CFv1
    component `component_v1_for_test`. Bundle this component with the test
    package. It prevents the actual input driver from interacting with the test.
    Invoke Root Presenter from the test package's URL. E.g.,
    `fuchsia-pkg://touch-input-test#meta/root_presenter.cmx`.
5.  Bundle [Scenic](/src/ui/scenic/BUILD.gn)'s `component_v2` with the test's
    package. It ensures that the test uses the Scenic it was built with. Invoke
    Scenic using the relative URL. E.g., `#meta/scenic.cm`.
6.  Don't invoke components from *another* test's package!
7.  No sleeps or waits, unless the API is deficient. Every action by the test is
    gated on a logical condition that the test can observe. E.g., inject touch
    events only when the test observes the child view is actually connected to
    the view tree and vending content to hit.

## Guidelines for writing integration tests

We have Fuchsia-based *products* built on the Fuchsia *platform*. As Fuchsia
platform developers, we want to ship a solid platform, and validate that the the
*platform* works correctly for all our supported *products*. Integration tests
ensure we uphold correctness and stability of platform functionality that spans
two or more components, via our prebuilt binaries (such as Scenic) and API
contracts (over FIDL). This is especially valuable in validating our ongoing
platform migrations. One example is the set of touch dispatch paths, such as
from Input Pipeline to Scenic to Flutter.

### Models of production

Integration tests model a specific product scenario by running multiple Fuchsia
components together. For example, to ensure that the "touch dispatch path from
device to client" continues to work as intended, we have a "touch-input-test"
that exercises the actual components involved in touch dispatch, over the actual
FIDLs used in production.

Because integration tests are a model, there can (and should) be some
simplification from actual production. Obviously, these tests won't run the
actual product binaries; instead, a reasonable stand-in is crafted for a test.
The idea is that it's the simplest stand-in that can be used in a test, which
still can catch serious problems and regressions.

Sometimes, it's not straightforward for the test to use an actual platform path
used in production; we use a reasonable stand-in for these cases too. For
example, we can't actually inject into `/dev/class/input-report`, so we have a
dedicated
[API surface](/sdk/fidl/fuchsia.input.injection/input_device_registry.fidl) on
Input Pipeline to accept injections in a test scenario.

The important thing is that the test gives us *confidence* that evolution of
platform code and platform protocols will not break existing product scenarios.

### No flakes

When the scenario involves graphics, it's very easy to accidentally introduce
flakiness into the test, robbing us of confidence in our changes. Graphics APIs
operate across several dimensions of lifecycle, topology, and
synchronization/signaling schemes, in the domains of components, graphical
memory management, view system, and visual asset placement. Furthermore, these
APIs provide the basis for, and closely interact with, the Input APIs and the
Accessibility APIs.

The principal challenge is to write tests that set up a real graphics stack, in
a way that is robust against elasticity in execution time. We talk about those
challenges in "Synchronization challenges", below. There are other reasons for
tests going wrong, and most of them can be dealt with by enforcing hermeticity
at various levels. We talk about these first. A final challenge is to author
tests that model enough of the interesting complexity on just the platform side,
so that we know complex product scenarios don't break with platform evolution.

### Precarious stack of stuff

At the bottom we have graphics tests. Input tests build on top of graphics
tests. And accessibility tests build on top of input tests. Hence they have all
the same problems, just with more components. It is thus critical that a basic
graphics test is reasonable to write and understand, because they form the basis
for "higher level" tests that inherently have more complexity.

### Questions and answers

#### Why not just rely on product-side e2e tests?

Product owners must write e2e tests to ensure their product is safe from
platform changes. E2e tests are big, heavy, and expensive to run; often, they
are flaky as well. They are authored in a different repository, and run in their
own test automation regime ("CQ"). And they care about the subset of OS
functionality that their product relies on.

Given these realities, platform developers cannot rely on these e2e tests to
catch problems in platform APIs and platform code.

By authoring platform-side integration tests, platform developers can get
breakage signals much faster with less code in the tests, and systematically
exercise all the functionality used across the full range of supported products.
Product owners benefit by increased confidence in the platform's reliability.

#### Why all this emphasis on hermeticity?

Deterministic, flake-free tests increase the signal-to-noise ratio from test
runs. They make life better.

When your tests rain down flakes every day, we ignore these tests, and they
become noise. But when we try to fix the source of flakes, it often reveals a
defect in our practices, or APIs, or documentation, which we can fix (think
"impact"). Each of these hermeticity goals address a real problem that someone
in Fuchsia encountered. When we have hermeticity, everyone benefits, and Fuchsia
becomes better.

#### Why all this emphasis on integration tests?

Fuchsia's platform teams have important migrations in progress that affect
products. Integration tests are a critical method of guaranteeing that our
platform changes are safe and stable with respect to our product partners.
Examples: Components Framework v2, Input API migration, Flatland API migration,
etc.

#### What about CTS tests?

The
[Fuchsia Compatibility Test Suite](/docs/contribute/governance/rfcs/0015_cts)
ensures that the implementations offered by the Fuchsia platform conform to the
specifications of the Fuchsia platform. An effective CTS will have UI
integration tests, and so this guidance doc applies to those UI integration
tests.

## Prefer hermeticity

Various types of
[hermeticity](/docs/concepts/testing/v2/test_runner_framework#hermeticity) make
our tests more reliable.

### Package hermeticity

All components used in the test should come from the same test package. This can
be verified by examining the fuchsia-pkg URLs launched in the test; they should
reference the test package.

If we don't have package hermeticity, and a component C is defined in the
universe U, then the C launched will come from U, instead of your locally
modified copy of C. This issue isn't so much a problem in CQ, because it
rebuilds everything from scratch. However, it is definitely an issue for local
development, where it causes surprises - another sharp corner to trap the
unwary. That is, a fix to C won't necessarily run in your test, and hampers
developer productivity.

There is a further advantage to package hermeticity. For those components that
read from the `config-data` package, this practice allows a test package to
define their own config-data for the components they contain. In fact, this is
the only way to define a piece of custom config-data for a test. For example,
the display rotation in Root Presenter is conveyed with config-data.

### Environment hermeticity

All components in the test should be brought up and torn down in a custom
Fuchsia environment. In component framework v2, the RealmBuilder is responsible
for rebuilding the component topology for each test run and shutting down the
components in an ordered manner.

This practice forces component state to be re-initialized on each run of the
test, thereby preventing inter-test state pollution.

The advantages of doing so are:

*   The test is far more reproducible, as the initial component state is always
    known to be good.
*   It's trivial to run the test hundreds of times in a tight loop, thus
    speeding up flake detection.
*   The test author can adjust the environment more precisely, and more
    flexibly, than otherwise possible.

#### No to `injected-services`

In component framework v2, it's possible to declare
[`injected-services`](https://fuchsia.dev/fuchsia-src/development/components/v2/migration/tests?hl=en#injected-services)
in a test's CML manifest. Declaring `injected-services` is somewhat of an
anti-pattern. It, too, also constructs a test environment, but *all the test
executions* run in the *same environment*. If a service component had dirtied
state, a subsequent `TEST_F` execution will inadvertently run against that
dirtied state.

### Capability hermeticity

All components in the test should not be exposed to the actual root environment.
For FIDL protocols, this is not so much an issue. However, there are other types
of capabilities where CF v1 has leaks. A good example is access to device
capabilities, such as `/dev/class/input-report` and
`/dev/class/display-controller`. Components that declare access to device
capabilities will actually access these capabilities, on the real device, in a
test environment.

We can gain capability hermeticity by relying on a reasonable fake. Two
examples.

*   The display controller, with some configuration, can be faked out. A
    subsequent advantage is that graphics tests can be run in parallel!
    *   One downside is that it's not easy to physically observe what the
        graphical state is, because the test no longer drives the real display.
        So development can be a little harder.
*   The input devices are faked out with an injection FIDL, and that's how tests
    can trigger custom input. However, the component that receives injections
    still needs to avoid declaring access to `/dev/class/input-report`! The
    recommendation here is to put a `/dev`-less copy of the component manifest
    into the test package.
    *   Example:
        [root_presenter.cmx](/src/ui/bin/root_presenter/meta/root_presenter.cmx)
        (production) vs
        [root_presenter_base.cmx](/src/ui/bin/root_presenter/meta/root_presenter_base.cmx)
        (production, minus `/dev/class/input-report`).

## Synchronization challenges

Correct, flake-free inter-component graphics synchronization depends intimately
on the specific graphics API being used. The
[legacy Scenic API](/sdk/fidl/fuchsia.ui.scenic/session.fidl), sometimes called
"GFX", has sparse guarantees for when something is "on screen", so extra care
must be taken to ensure a flake free test. As a rule of thumb, if you imagine
the timeline of actions for every component stretching and shrinking by
arbitrary amounts, a robust test will complete for all CPU-schedulable
timelines. The challenge is to construct action gates where the test will hold
steady until a desired outcome happens. Sleeps and timeouts are notoriously
problematic for this reason. Repeated queries of global state (such as a pixel
color test) are another mechanism by which we could construct higher-level
gates, but incur a heavy performance penalty and adds complexity to debugging.

Another dimension of complexity is that much of client code does not interact
directly with Fuchsia graphics APIs; instead they run in an abstracted runner
environment. Flutter and Web are good examples where the client code cannot
directly use Scenic APIs. Some facilities can be piped through the runner, but
tests generally cannot rely on full API access. Some runners even coarsen the
timestamps, which also complicates testing a bit.

One more subtlety. We're interested in the "state of the scene graph", which is
not precisely the same thing as "state of the rendering buffer". For most
purposes, they are loosely equivalent, because the entity taking a visual
screenshot is the same entity that holds the scene graph - Scenic. However,
specific actions, like accessibility color adjustments, will not be accurately
portrayed in a visual screenshot, because the color adjustment takes place in
hardware, below Scenic.

## Using the View Observer Protocol

Use `fuchsia.ui.observation.geometry.Provider` to correctly know when a view is
present in the scene graph. Refer [here](/src/ui/tests/view_observer_guide.md)
for more details on how to use the API

### Test setup - Realm Builder

The
[Touch Input Test](/src/ui/tests/integration_input_tests/touch/touch-input-test.cc)
is constructed using the Realm Builder
[library](/docs/development/testing/components/realm_builder). This library is
used to construct the test [Realm](/docs/concepts/components/v2/realms) in which
the components under test operate. The test suite, hereafter test driver
component, is a v2 component. This is in contrast to the components in the
constructed realm, e.g. `scenic`, that are at the moment v1 components. This is
so because Realm Builder provides a v1 "bridge" that allows for Realms
constructed to include v1 and v2 components.

Realm Builder (and CFv2 in general) allows us to be explicit about what
capabilities are routed to and from components. This is crucial for testing
because it allows test authors to have fine-grained control over the test
environment of their components. Take for example `scenic`. In
`touch-input-test`, a handle to `fuchsia.hardware.display.Provider` from
`fake-hardware-display-controller-provider#meta/hdcp.cmx` is routed. By
providing a fake hardware display provider, we can write integration tests
without having to use the real display controller. This mapping of source and
target is explicitly written in the test
[file](/src/ui/tests/integration_input_tests/touch/touch-input-test.cc) during
Realm construction.

Since the Realm is populated by v1 components at the moment, it's worth
mentioning how this works at a high level. Components added to a Realm
constructed with Realm Builder work in the same way as typical Realm
construction statically. In other words, Realm Builder doesn't do anything
special. It simply allows for the construction of Realms at runtime, instead of
statically via manifests. It generates the necessary manifests and clauses, e.g.
offer capability foo to #child-a, behind the scene when a user invokes a method
like `realm_builder->AddRoute(...)`. For v1 components, this is somewhat still
true. The major difference is that Realm Builder constructs a wrapper v2
component to launch a v1 component. When the wrapper component is started, it
launches the v1 component by creating a singleton
`fuchsia.sys.NestedEnvironment`. This NestedEnvironment will only contain the v1
component, and won't contain any services except for those explicitly routed to
it during Realm construction.

A sample diagram of a Realm constructed with RealmBuilder would look like:

```
    <root>
    /    \
```

<wrapper> <wrapper> / \
scenic.cmx root_presenter.cmx

In production, the environment would look more like:

```
 <appmgr>
     |
 <modular>
 /       \
```

scenic.cmx root_presenter.cmx

## Modeling complex scenarios

The graphics API allows each product to generate an arbitrarily complex scene
graph. However, the products we have today typically rely on a few "core
topologies" that are stable and suitable for the product to build on.

It's a valuable exercise to capture each of these core topologies in our
platform integration tests. Some examples:

*   Touch dispatch to the Flutter runner.
    *   The intra-component connection between the Flutter runner (C++) and the
        Flutter framework (Dart) is delicate, and a runner test will catch bad
        rolls into fuchsia.git. Workstation was once broken for many months due
        to a problem here.
*   Touch dispatch to the Chromium runner.
    *   Chromium employs a two-view topology as part of its JS sandboxing
        strategy. Having a test ensures that Chromium is correctly using our
        APIs, and that our changes don't accidentally break Chromium.
*   Touch dispatch from Flutter runner to Chromium runner.
    *   This scenario models a critical production path where the product
        reinjects touch events into its Chromium child view.
*   One parent view and two child views, using assorted runners, to ensure touch
    dispatch is routed to the correct view.

Developing new models are also how we test new topologies and interaction
patterns to make sure the APIs are sensible and usable, and serve as as a
foundation for converting an entire product.