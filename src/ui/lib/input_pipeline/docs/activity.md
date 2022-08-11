# input_pipeline > Activity

Reviewed on: 2022-08-10

`activity` is a library for creating an activity service.

## Purpose

The activity service, implemented as `ActivityManager`, is a service which
reports whether user input activity has happened recently as reported to
the input pipeline.

## States

The service is `Active` on startup and will remain `Active` as it receives
activity reports. The service will transition to `Idle` after a certain
amount of time, known as the idle transition threshold, has transpired since
the last activity was reported to the service, e.g. 15 minutes.

Products may configure this threshold using structured configuration.

If the service receives activity reports after transitioning to `Idle`, it
will transition back to the `Active` state.

## Reporting activity

`InputHandlers` registered in the input pipeline can report to the activity
service via
`fuchsia.input.interaction.observation.Aggregator/ReportDiscreteActivity`,
which will take the time of the activity and serve an acknowledgement of
reception.

Activities are rate-limited, meaning that the activity service may
transition to an idle state earlier than the idle transition threshold by
up to the rate-limit amount.

For example, if the idle transition threshold is 15 minutes, and activities
within the same 1 minute are rate-limited, then activities reported at time
0 and time 59 seconds will still yield a transition to the Idle state at
time 15 minutes.

Clients may also want to throttle activity reports on their own ends as
well, and as a baseline might consider sending no more than one report per
second.

### Example usage

```rust
use fidl_fuchsia_input_interaction_observation as interaction_observation;
use fuchsia_component::client::connect_to_protocol;

let aggregator_proxy =
    connect_to_protocol::<interaction_observation::AggregatorMarker>()?;
aggregator_proxy.report_discrete_activity(
    event_time.into_nanos()).await.expect("Failed to report activity");
```

## Subscribing to activity state

Clients can subscribe to the activity service's transitions in activity
state via `fuchsia.input.interaction.Notifier/WatchState`, which follows a
hanging-get pattern.

The server will always respond immediately with the initial state, and
after that whenever the system's state changes.

### Example usage

```rust
use async_utils::hanging_get::client::HangingGetStream;
use fidl_fuchsia_input_interaction::{NotifierMarker, NotifierProxy};
use fuchsia_component::client::connect_to_protocol;

let notifier_proxy = connect_to_protocol::<NotifierMarker>()?;
let mut watch_activity_state_stream =
    HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);

while let Some(Ok(state)) = watch_activity_state_stream.next().await {
    match state {
        State::Active => {/* do something */},
        State::Idle => {/* do something */}
    }
}
```

## Configuring the idle transition threshold with product assembly

The activity service uses structured configuration by declaring a `config`
value in an input pipeline component manifest, such as in
`//src/ui/bin/input-pipeline/meta/input-pipeline.cml` or
`//src/ui/bin/scene_manager/meta/scene_manager.cml`:

```
config: {
    idle_threshold_minutes: { type: "int64" },
},
```

This value can also be set in product assembly via the following
configuration schema:

```
product_assembly_configuration("my_product") {
  platform = {
    input = {
      idle_threshold_minutes = 15
    }
  }
}
```

It can also be set to a default value such as for testing:

```
fuchsia_structured_config_values("test_config") {
  cm_label = ":manifest"
  values = {
    idle_threshold_minutes = 2
  }
}
```

## Future work

### Idleness beyond the threshold

Some clients may be interested in implementing functionality far deeper
into idleness than the service currently supports. As more concrete use
cases arise, the service could be extended to meet growing needs. In the
mean time, there are still ways the service can be used to meet certain
goals.

For example, if the activity service transitioned to idle 15 minutes after
the last user input activity, but a client wanted to do something only
after it knew that a user has been idle for an hour, it is still possible
to use the current API to accomplish these goals.

It is recommended in this case to still subscribe to activity state changes
via `fuchsia.input.interaction.Notifier/WatchState` and implement your own
timers beyond. One such approach might look like:

1. Start a 45-minute timer after receiving an `Idle` state.
2. If the service reports an `Active` state before the timer elapses,
   cancel the timer.
3. If the timer does elapse, the client can have confidence that the state
   has been idle for one hour in total and do work accordingly.
