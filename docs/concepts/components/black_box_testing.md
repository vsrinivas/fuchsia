# Black box testing with component manager

## Motivation

The black box testing framework enables an integration test with component
manager to observe or influence the behavior of component manager without
depending on its internal libraries.

Creating dependencies on component manager's internal libraries is problematic
for a number of reasons:

- A test can set up component manager inconsistently with how component manager
is normally started.
- A test can modify component manager’s behavior in arbitrary ways.
- Changes to component manager may require changing the test.

## Features

To test the behavior of a component or a component manager feature, you must be
able to write a black box test that can:

- Start component manager in a hermetic environment.
- Communicate with component manager using only FIDL and the [hub](hub.md)
- Access the hub of the root component.
- Wait for events to occur in component manager.
- Halt a component manager task on an event.
- Inject framework or builtin capabilities into component manager.

Note: The black box testing framework covers all of these points.

## Libraries

The testing framework provides two Rust libraries for black box testing:

- `BlackBoxTest`
- `BreakpointSystemClient`

### BlackBoxTest

`BlackBoxTest` is a Rust library provided by the testing framework. You can use
the classes and methods in this library to automate large parts of the setup
needed for a black box test.

#### Minimum requirements

For the `BlackBoxTest` library to function correctly, the integration test
component manifest must specify (at minimum) the following features and
services:

```rust
"sandbox": {
    "features": [
        "hub"
    ],
    "services": [
        "fuchsia.process.Launcher",
        "fuchsia.sys.Launcher",
        "fuchsia.sys.Environment",
        "fuchsia.logger.LogSink"
    ]
}
```

These services and features ensure that `BlackBoxTest` can set up a hermetic
environment and launch component manager.

#### Usage

In the simplest case, a black box test looks like the following:

```rust
let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/foo#meta/root.cm").await?;
```

By the end of this statement:

- A component manager instance has been created in a hermetic environment.
- The root component is specified by the given URL.
- Component manager is waiting to be unblocked by the breakpoint system.
- The root [component manifest](component_manifests.md) (`root.cm`) has been
resolved.
- No component has been started.
- Component manager’s outgoing directory is serving:
  - The hub of the root component at `$out/hub`.
  - The `BreakpointSystem` FIDL service at
    `$out/svc/fuchsia.test.breakpoints.BreakpointSystem`.
- The state of the hub reflects the following:
  - Static children of the root component should be visible.
  - Grandchildren of the root component should not be visible (because they
  haven't been resolved yet).
  - There should be no `exec` directories for any component.

The `BreakpointSystem` FIDL service is used to set breakpoints and unblock
component manager. This code demonstrates using the `BreakpointSystem` service:

```rust
let breakpoint_system = test.connect_to_breakpoint_system().await?;
let receiver = breakpoint_system.set_breakpoints(vec![StopInstance::TYPE]).await?;
breakpoint_system.start_component_manager().await?;
```

By the end of this code block:

- A breakpoint receiver has been created which listens to `StopInstance` events.
- Component manager’s execution has begun.
- The root component (and its eager children, if any) will be started soon.

#### Custom tests and convenience methods

In some cases, you may want to customize `BlackBoxTest::default`.
`BlackBoxTest::custom` allows you to specify:

- The component manager manifest to be used for the test.

- Additional directories to be created in component manager's namespace.

- A file descriptor to redirect output from components.

```rust
let test = BlackBoxTest::custom(
    "fuchsia-pkg://fuchsia.com/my_custom_cm#meta/component_manager.cmx",
    "fuchsia-pkg://fuchsia.com/foo#meta/root.cm",
    vec![("my_dir", my_dir_handle)],
    output_file_descriptor
).await?;
```

The `BlackBoxTest` library also provides convenience methods for starting up
component manager and expecting a particular output:

```rust
launch_component_and_expect_output(
    "fuchsia-pkg://fuchsia.com/echo#meta/echo.cm",
    "Hippos rule!",
).await?;
```

### BreakpointSystemClient

The breakpoint system addresses the problem of verifying state in component
manager and is analogous to a debugger's breakpoint system.

Since the breakpoint system is built on top of system events:

- A breakpoint can only be set on a system event.
- It supports all system events in component manager.
- It can be scoped down to a [realm](realms.md) of the component hierarchy.
- It follows the component manager’s rules of event propagation (i.e - an
event dispatched at a child realm is also dispatched to its parent).

Note: When component manager is in [debug mode](#debug-mode), the breakpoint
system is installed at the root. Hence it receives events from all components.

For reliable state verification, a test must be able to:

- Expect or wait for various events to occur in component manager.
- Halt the component manager task that is processing the event.

The workflow for the `BreakpointSystemClient` library looks something like this:

```rust
// Create a BreakpointSystemClient using ::new() or use the client
// provided by BlackBoxTest
let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/foo#meta/root.cm").await?;

// Get a receiver by setting breakpoints
let breakpoint_system = test.connect_to_breakpoint_system().await?;
let receiver = breakpoint_system.set_breakpoints(vec![StartInstance::TYPE]).await?;

// Unblock component manager
breakpoint_system.start_component_manager().await?;

// Wait for an invocation
let invocation = receiver.expect_type::<StartInstance>().await?;

// Verify state
...

// Resume from invocation
invocation.resume().await?;
```

With complex component hierarchies, event propagation is hard to predict and
may even be non-deterministic due to the asynchronous nature of component
manager. To deal with these cases, breakpoints offer the following additional
functionality:

- [Multiple receivers](#multiple-receivers)
- [Discardable receivers](#discardable-receivers)
- [Capability injection](#capability-injection)
- [Event sinks](#event-sinks)

#### Multiple receivers {#multiple-receivers}

It is possible to register multiple receivers, each listening to their own set
of events:

```rust
// StartInstance and RouteFrameworkCapability events can be interleaved,
// so use different receivers.
let start_receiver = breakpoint_system.set_breakpoints(vec![StartInstance::TYPE]).await?;
let route_receiver =
    breakpoint_system.set_breakpoints(vec![RouteFrameworkCapability::TYPE]).await?;

// Expect 5 components to start
for _ in 1..=5 {
    let invocation = start_receiver.expect_type::<StartInstance>().await?;
    invocation.resume().await?;
}

// Expect a RouteFrameworkCapability event from /foo:0
let invocation =
    route_receiver.expect_exact::<RouteFrameworkCapability>("/foo:0").await?;
invocation.resume().await?;
```

#### Discardable receivers {#discardable-receivers}

It is possible to listen for specific invocations and then discard the receiver,
causing future invocations to be ignored:

```rust
// Set a breakpoint on StopInstance events
let stop_receiver = breakpoint_system.set_breakpoints(vec![StopInstance::TYPE]).await?;

{
    // Temporarily set a breakpoint on UseCapability events
    let use_receiver = breakpoint_system.set_breakpoints(vec![UseCapability::TYPE]).await?;

    // Expect a UseCapability event from /bar:0
    let invocation = route_receiver.expect_exact::<UseCapability>("/bar:0").await?;
    println!("/bar:0 used capability -> {}", invocation.capability_path);
    invocation.resume().await?;
}

// At this point, the test does not care about UseCapability events, so the receiver
// can be dropped. If the receiver were left instantiated, component manager would
// halt on future UseCapability events.

// Expect a StopInstance event
let invocation = stop_receiver.expect_type::<StopInstance>().await?;
println!("{} was stopped!", invocation.target_moniker);
invocation.resume().await?;
```

#### Capability injection {#capability-injection}

Several tests need to communicate with components directly. The simplest way
to do this is for a component to connect to a capability offered by the test.

It is possible to listen for a `RouteFrameworkCapability` or
`RouteBuiltinCapability` event and inject an external capability provider:

```rust
// Create the server end of EchoService
let echo_service = EchoService::new();

// Set a breakpoint on RouteFrameworkCapability events
let receiver =
    breakpoint_system.set_breakpoints(vec![RouteFrameworkCapability::TYPE]).await?;

// Wait until /foo:0 attempts to connect to the EchoService framework capability
let invocation = receiver.wait_until_route_framework_capability(
    "/foo:0",
    "/svc/fuchsia.echo.EchoService",
).await?;

// Inject the EchoService capability
let serve_fn = echo_capability.serve_async();
invocation.inject(serve_fn).await?;

// Resume from the invocation
invocation.resume().await?;
```

#### Event sinks {#event-sinks}

It is possible to soak up events of certain types and drain them at a later
point in time:

```rust
let receiver = breakpoint_system.set_breakpoints(vec![PostDestroyInstance::TYPE]).await?;
let sink = breakpoint_system.soak_events(vec![StartInstance::TYPE]).await?;

// Wait for the root component to be destroyed
let invocation = receiver.expect_exact::<PostDestroyInstance>("/").await?;
invocation.resume().await?;

// Drain events from the sink
let events = sink.drain().await;

// Verify that the 3 components were started in the correct order
assert_eq!(events, vec![
    DrainedEvent {
        event_type: StartInstance::TYPE,
        target_moniker: "/".to_string()
    },
    DrainedEvent {
        event_type: StartInstance::TYPE,
        target_moniker: "/foo:0".to_string()
    },
    DrainedEvent {
        event_type: StartInstance::TYPE,
        target_moniker: "/foo:0/bar:0".to_string()
    }
]);
```

## Debug Mode {#debug-mode}

Both `BlackBoxTest` and `BreakpointSystemClient` rely on component manager’s
debug mode.

To start component manager in debug mode, pass in `--debug` as an additional
argument to the `component_manager.cmx` component. In fact, this is exactly what
`BlackBoxTest::default` does when setting up a black box test.

When component manager is in debug mode, it does the following:

1. Creates the root realm and built-in services.

1. Creates the hub and the breakpoint system.

1. Serves the following from component manager's outgoing directory:

   - The hub of the root component at `$out/hub`.

   - The `BreakpointSystem` FIDL service at
   `$out/svc/fuchsia.test.breakpoints.BreakpointSystem`.

1. Waits to be unblocked by the `BreakpointSystem` FIDL service.

1. Starts up the root component (including any eager children).
