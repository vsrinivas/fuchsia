# Flatland Frame Scheduling Lib

This library contains a couple of example implementations of how to use Flatland's frame scheduling
API (`fuchsia.ui.composition.Flatland`). They're available to be copied as a basis for writing your
own scheduler, or for use as-is.

For an example of usage, look at `src/ui/examples/flatland-view-provider/main.rs`.

## Schedulers

These are the example scheduler implementations included:

- `NaiveScheduler`
  - A basic scheduler pushing out frames indiscriminately. Written to be as easy to read as
    possible. Not recommended for actual use.

The rest are slightly more complicated as they implement futures, but they use the same principles.

- `ThroughputScheduler`
  - Tries to schedule frames as often as possible, with as much leeway as possible.
  - This is the recommended usage for most applications. It should provide the highest frame rate
    at the cost of some latency and occasional dropped frames.

- `UnsquashableScheduler`
  - Similar to `ThroughputScheduler`, except frames are never dropped (but they may be delayed) at
    the cost of latency.

- `LowLatencyScheduler`
  - This scheduler is for achieving the lowest possible latency, but it requires the client to
    manually set how much time it expects to need to prepare a frame.

## Stats Collecting Wrappers

These are wrappers around the schedulers, used to collect performance stats. They provide the same
interface as the schedulers, and can be used as-is, or as a demonstration for how to collect data
using the same API.

- `FrameStatsCollector`
  - Collects raw frame stats, such as how many were drawn and dropped, and average latencies and
    frame rates.

- `LatencyStatsCollector`
  - This one only works with the `LowLatencyScheduler`.
  - Is itself a wrapper around `FrameStatsCollector`, but provides additional data for how successful
    the run was on a per-latency value used basis.

## Usage

All the schedulers in the library expose the same interface and are expected to be used the same
way. They're expected to be waited on in a `select!` loop, with `OnFramePresented()` and
`OnNextFrameBegin()` being forwarded to the scheduler as soon as the events arrive (though not all
events are actually used by all schedulers).
As soon as the `wait_to_update()` future completes the client is expected to update their frame
and call `Present()` with the values returned from `wait_to_update()`.
Finally, in order not to create unnecessary updates when idle, `wait_to_update()` will not actually
complete until a call has been made to `request_present()` (except for in the `NaiveScheduler`
case).

The library takes no dependencies on the Flatland rust bindings themselves, so the types must be
translated between the ones defined by the library and the ones used in
`fuchsia.ui.composition.Flatland`.

Here's a slightly pared down version of how it's used in the `FlatlandViewProvider` example.

```rust
let sched_lib = ThroughputScheduler::new();

loop {
  futures::select! {
    message = internal_receiver.next().fuse() => {
      if let Some(message) = message {
        match message {
            MessageInternal::OnNextFrameBegin {
                additional_present_credits,
                future_presentation_infos,
            } => {
              let infos = future_presentation_infos
              .iter()
              .map(
                |x| PresentationInfo{
                  latch_point: zx::Time::from_nanos(x.latch_point.unwrap()),
                  presentation_time: zx::Time::from_nanos(x.presentation_time.unwrap())
                })
              .collect();
              sched_lib.on_next_frame_begin(additional_present_credits, infos);
            }
            MessageInternal::OnFramePresented { frame_presented_info } => {
              let presented_infos = frame_presented_info.presentation_infos
              .iter()
              .map(|info| PresentedInfo{
                present_received_time:
                  zx::Time::from_nanos(info.present_received_time.unwrap()),
                actual_latch_point:
                  zx::Time::from_nanos(info.latched_time.unwrap()),
              })
              .collect();

              sched_lib.on_frame_presented(
                zx::Time::from_nanos(frame_presented_info.actual_presentation_time),
                presented_infos);
            }
          }
      }
    }
    present_parameters = sched_lib.wait_to_update().fuse() => {
      app.update_scene(present_parameters.expected_presentation_time);
      flatland
          .present(fland::PresentArgs {
              requested_presentation_time: Some(present_parameters.requested_presentation_time.into_nanos()),
              acquire_fences: None,
              release_fences: None,
              unsquashable: Some(present_parameters.unsquashable),
              ..fland::PresentArgs::EMPTY
          })
          .unwrap_or(());

      // Call request_to_present() immediately to create frame continuously.
      sched_lib.request_present();
    }
  }
}
```

## `LowLatencyScheduler` Extras

The `LowLatencyScheduler` exposes one more property: the `latch_offset`. The `latch_offset` is the
duration the scheduler should try to give between completing `wait_to_update()` and when the
`Present()` call should be made. For the lowest possible latency this duration should be as short
as possible while keeping the number of dropped frames at acceptable levels. Careful measurements
must be used to use this fully.

The `latch_offset` can be updated at any time using:

```rust
sched_lib.latch_offset.set(calculate_next_latch_offset());
```

## Stats Collecting Wrapper Usage

`FrameStatsCollector` and `LatencyStatsCollector` are used in the same way as the other schedulers,
with some minor differences.

First, initialization is slightly different, since they take a reference to a different scheduler:

```rust
let inner_sched_lib = LowLatencyScheduler::new(/*latch_offset*/zx::Duration::from_millis(8));
let sched_lib = LatencyStatsCollector::new(&inner_sched_lib);
```

During use, the wrapper must be the thing waited on. That means `sched_lib` and not
`inner_sched_lib` in this example:

```rust
present_parameters = sched_lib.wait_to_update().fuse() => ...
```

Finally, the stats collectors exposes one more method returning the current stats as a string:
`stats_to_string()`.

Note: For LowLatencyScheduler, the `latch_offset` would still be set on `inner_sched_lib`.