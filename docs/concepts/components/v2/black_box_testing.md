# Black box testing with component manager

## Motivation

The black box testing framework enables an integration test to observe or
influence the behavior of component manager without depending on its internal
libraries.

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
- Communicate with component manager using only FIDL and the [hub](hub.md).
- Access the hub of the root component.
- Wait for events to occur in component manager.
- Halt a component manager task on an event.
- Inject or mock out capabilities.
- Interpose between a client and a service.

Note: The black box testing framework covers all of these points.

## Libraries

The testing framework provides two Rust libraries for black box testing:

- `BlackBoxTest`
- `EventSource`

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
- Component manager is waiting to be unblocked by the `EventSource`.
- The root [component manifest](component_manifests.md) (`root.cm`) has been
resolved.
- No component has been started.
- Component manager’s outgoing directory is serving:
  - The hub of the root component at `$out/hub`.
  - The `BlockingEventSource` FIDL service at
    `$out/svc/fuchsia.sys2.BlockingEventSource`.
- The state of the hub reflects the following:
  - Static children of the root component should be visible.
  - Grandchildren of the root component should not be visible (because they
  haven't been resolved yet).
  - There should be no `exec` directories for any component.

Use the `BlockingEventSource` FIDL service to subscribe to events and unblock
the component manager. The following example shows you how to use the
`BlockingEventSource` service:

```rust
let event_source = test.connect_to_event_source().await?;
let event_stream = event_source.subscribe(vec![Stopped::TYPE]).await?;
event_source.start_component_tree().await?;
```

By the end of this code block:

- An `event_stream` has been created which receives `Stopped` events.
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

### EventSource

The `EventSource` addresses the problem of verifying state in component
manager and is analogous to a debugger's breakpoint system.

Since the `EventSource` is built on top of system events:

- A subscription can only be set on a system event.
- It supports all system events in component manager.
- It can be scoped down to a [realm](realms.md) of the component hierarchy.
- It follows the component manager’s rules of event propagation (i.e - an
event dispatched at a child realm is also dispatched to its parent).

Note: When component manager is in [debug mode](#debug-mode), an `EventSource`
is installed at the root. Hence it receives events from all components.

For reliable state verification, a test must be able to:

- Expect or wait for various events to occur in component manager.
- Halt the component manager task that is processing the event.

The workflow for the `EventSource` library looks something like this:

```rust
// Create a EventSource using ::new() or use the client
// provided by BlackBoxTest
let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/foo#meta/root.cm").await?;

// Get an event stream of the `Started` event.
let event_source = test.connect_to_event_source().await?;
let event_stream = event_source.subscribe(vec![Started::TYPE]).await?;

// Unblock component manager
event_source.start_component_tree().await?;

// Wait for an event
let event = event_stream.expect_type::<Started>().await?;

// Verify state
...

// Resume from event
event.resume().await?;
```

### Scoping of events

The `EventSource` can be requested by any component instance within the
component topology served by the component manager if its available to the component.
Events are capailities themselves so they have to be requested as well. Refer
to [event capabilities][event-capabilities] for more details on this.

A component instance can request a scoped `BlockingEventSource` in its manifest
file as follows:

```
{
    "program": {
        "binary": "bin/client",
    },
    "use": [
        {
            "protocol": [
                "/svc/fuchsia.sys2.BlockingEventSource",
            ],
            "from": "framework"
        },
        {
          event: [ "started", "stopped" ],
          from: "framework",
        }
    ],
}
```

Another component can pass along its scope of system events by passing along the
`BlockingEventSource` capability through the conventional routing operations `offer`,
`expose` and `use`.

If a component requests a `BlockingEventSource` then its children cannot start until it explicitly
calls `start_component_tree`.

### Additional functionality

With complex component hierarchies, event propagation is hard to predict and
may even be non-deterministic due to the asynchronous nature of component
manager. To deal with these cases, `EventSource` offers the following additional
functionality:

- [Multiple event streams](#multiple-event-streams)
- [Discardable event streams](#discardable-event-streams)
- [Capability injection](#capability-injection)
- [Capability interposition](#capability-interposition)
- [Event logs](#event-logs)

#### Multiple event streams {#multiple-event-streams}

It is possible to register multiple event streams, each listening to their own set
of events:

```rust
// Started and CapabilityRouted events can be interleaved,
// so use different event streams.
let start_event_stream = event_source.subscribe(vec![Started::TYPE]).await?;
let route_event_stream =
    event_source.subscribe(vec![CapabilityRouted::TYPE]).await?;

// Expect 5 components to start
for _ in 1..=5 {
    let event = start_event_stream.expect_type::<Started>().await?;
    event.resume().await?;
}

// Expect a CapabilityRouted event from ./foo:0
let event = route_event_stream.expect_exact::<CapabilityRouted>("./foo:0").await?;
event.resume().await?;
```

#### Discardable event streams {#discardable-event-streams}

It is possible to listen for specific events and then discard the event stream,
causing future events to be ignored:

```rust
// Subscribe to Stopped events
let stop_event_stream = event_source.subscribe(vec![Stopped::TYPE]).await?;

{
    // Temporarily subscribe to CapabilityRouted events
    let use_event_stream = event_source.subscribe(vec![CapabilityRouted::TYPE]).await?;

    // Expect a CapabilityRouted event from ./bar:0
    let event = route_event_stream.expect_exact::<CapabilityRouted>("./bar:0").await?;
    println!("/bar:0 used capability -> {}", event.capability_id);
    event.resume().await?;
}

// At this point, the test does not care about CapabilityRouted events, so the
// event stream can be dropped. If the event stream were left instantiated,
// component manager would halt on future CapabilityRouted events.

// Expect a Stopped event
let event = stop_event_stream.expect_type::<Stopped>().await?;
println!("{} was stopped!", event.target_moniker);
event.resume().await?;
```

#### Capability injection {#capability-injection}

Several tests need to communicate with components directly. It is often also
desirable to mock out capabilities that a component connects to in the test. The
simplest way to do this is to implement an `Injector`.

```rust
/// Client <---> EchoCapability
/// EchoCapability implements the Echo protocol and responds to clients.
struct EchoCapability;

#[async_trait]
impl Injector for EchoCapability {
    type Marker = fecho::EchoMarker;

    async fn serve(self: Arc<Self>, mut request_stream: fecho::EchoRequestStream) {
        // Start listening to requests from client
        while let Some(Ok(fecho::EchoRequest::EchoString { value: Some(input), responder })) =
            request_stream.next().await
        {
            // Respond to the client with the echo string.
            responder.send(Some(&input)).expect("failed to send echo response");
        }
    }
}
```

The `BlockingEventSource` can automatically install an injector
matching the capability requested by any component in the test.

```rust
let echo_capability = EchoCapability::new();
event_source.install_injector(echo_capability, None).await?;

// Subscribe to other events.

event_source.start_component_tree().await?;
```

#### Capability interposition {#capability-interposition}

Tests may want to silently observe or mutate messages between a client
and service. It is possible to interpose a capability and manipulate the traffic
over the channel. Consider an interposer for an Echo service that mutates the input from
the client before sending it to the service:

```rust
/// Client <---> EchoInterposer <---> Echo service
/// The EchoInterposer copies all echo responses from the service
/// and sends them over an mpsc::Channel to the test.
struct EchoInterposer;

#[async_trait]
impl Interposer for EchoInterposer {
    type Marker = fecho::EchoMarker;

    async fn interpose(
        self: Arc<Self>,
        mut from_client: fecho::EchoRequestStream,
        to_service: fecho::EchoProxy,
    ) {
        // Start listening to requests from client
        while let Some(Ok(fecho::EchoRequest::EchoString { value: Some(input), responder })) =
            from_client.next().await
        {
            // Copy the response from the service and send it to the test
            let modified_input = format!("{} Let there be chaos!", input);

            // Forward the request to the service and get a response
            let out = to_service
                .echo_string(Some(&modified_input))
                .await
                .expect("echo_string failed")
                .expect("echo_string got empty result");

            // Respond to the client with the response from the service
            responder.send(Some(out.as_str())).expect("failed to send echo response");
        }
    }
}
```

The `BlockingEventSource` can automatically install an interposer
matching the capability requested by any component in the test.

```rust
let (interposer, mut rx) = EchoInterposer::new();
event_source.install_interposer(interposer, None).await?;

// Subscribe to other events.

event_source.start_component_tree().await?;
```

#### Event logs {#event-logs}

It is possible to record events of certain types asynchronously and flush them at a later
point in time:

```rust
let event_stream = event_source.subscribe(vec![Destroyed::TYPE]).await?;
let event_log = event_source.record_events(vec![Started::TYPE]).await?;

// Wait for the root component to be destroyed
let event = event_stream.expect_exact::<Destroyed>(".").await?;
event.resume().await?;

// Flush events from the log
let events = event_log.flush().await;

// Verify that the 3 components were started in the correct order
assert_eq!(events, vec![
    RecordedEvent {
        event_type: Started::TYPE,
        target_moniker: "./".to_string()
    },
    RecordedEvent {
        event_type: Started::TYPE,
        target_moniker: "./foo:0".to_string()
    },
    RecordedEvent {
        event_type: Started::TYPE,
        target_moniker: "./foo:0/bar:0".to_string()
    }
]);
```

Note that recording of events will continue until the `EventLog` object goes out
of scope.

## Debug Mode {#debug-mode}

Both `BlackBoxTest` and `EventSource` rely on component manager’s
debug mode.

To start component manager in debug mode, pass in `--debug` as an additional
argument to the `component_manager.cmx` component. In fact, this is exactly what
`BlackBoxTest::default` does when setting up a black box test.

When component manager is in debug mode, it does the following:

1. Creates the root realm and built-in services.

1. Creates the hub and the `EventSource`.

1. Serves the following from component manager's outgoing directory:

   - The hub of the root component at `$out/hub`.

   - The `BlockingEventSource` FIDL service at
   `$out/svc/fuchsia.sys2.BlockingEventSource`.

1. Waits to be unblocked by the `BlockingEventSource` FIDL service.

1. Starts up the root component (including any eager children).

[event-capabilities]: capabilities/event.md
