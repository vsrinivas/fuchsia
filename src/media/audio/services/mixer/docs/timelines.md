# Audio Mixer Service: Timelines

[TOC]

## Reference time, media time, and frame time

Every edge in the mix graph represents a single audio stream, where each audio
stream contains a sequence of timestamped frames. We measure the progress of
time in three ways:

*   **Reference time** is a `zx::time` value relative to a specific `zx::clock`
    [reference clock](clocks.md). Reference time advances continuously and
    monotonically and is always expressed in nanoseconds relative to the clock's
    epoch.

*   **Media time** represents the logical sequence of frames produced or
    consumed by a Producer/Consumer node. Media time advances when the node is
    started and stops advancing when the node is stopped. Media time is defined
    at Producers and Consumers only -- it is not defined at any other graph
    nodes.

    When a Producer/Consumer node is backed by a
    [StreamSink](https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.audio.mixer/node_options.fidl;drc=e859bd380655c4be3890af5625de9a2e15984c11;l=26)
    (i.e., a packet queue), packet timestamps are expressed in media time and
    the client can define media time using any units they like. Common choices
    are nanoseconds and frames. For example, a 30 second audio stream might
    start at media timestamp 0 and end at media timestamp 30,000,000,000 (in
    nanosecond units) or 30,000,000 (in millisecond units) or 1,440,000 (in
    frame units assuming 48kHz audio).

    When a Producer/Consumer node is backed by a ring buffer, media time always
    has units frames, where each media timestamp corresponds to a unique
    position in the ring buffer.

*   **Frame time** is used internally by the mixer and is not accessible to
    clients. Frame time behaves similarly to media time but always has units
    frames. Every PipelineStage operates on frame time internally. Producer and
    Consumer stages are responsible for translating media times to-and-from
    frame times.

## Presentation timestamps

For each audio frame, the most important reference time is the frame's
**presentation timestamp**. For output pipelines, this is the time the frame
will be presented (i.e. rendered) at a speaker, at the end of the pipeline. For
input pipelines, this is the time the frame was initially presented (i.e.
captured) at a live microphone, at the beginning of the pipeline.

## Translating between media time and presentation time

Each call to [`Graph.Start`](TODO<fxbug.dev/87651>: add link) links a pair of
timestamps *(Tr0, Tm0)*, where reference time *Tr0* is media time *Tm0*'s
*presentation timestamp*. Given this pair plus the units for media time, we can
build a
[TimelineFunction](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/media/audio/lib/timeline/timeline_function.h;drc=b076cf49545244228ad4ba0ba2b48582b6cb76a6;l=21),
to translate between media timestamps and presentation timestamps. This function
has the following coefficients:

*   The pair *(Tr0, Tm0)* provided by `Graph.Start` defines the
    TimelineFunction's epoch. The first frame, at *Tm0*, has presentation
    timestamp *Tr0*.

*   A rate *∆m / ∆r*, where every *∆m* steps of media time correspond to *∆r*
    steps of reference time. If the media timeline is stopped, this rate is 0.
    If media time has units nanoseconds, this rate is 1. If media time has units
    frames, this is the frame rate.

While an audio stream is running, the above translation is defined for all
timestamps from *(Tr0, Tm0)* and higher. When
[Graph.Stop](TODO<fxbug.dev/87651>: add link) is called to stop the audio stream
at time *(Tr1, Tm1)*, media time stops advancing which makes the translation
undefined. By convention, we represent the translation between presentation and
media times as a `std::optional<TimelineFunction>` which is `std::nullopt` iff
the stream is stopped.

`Graph.Start` and `Graph.Stop` behave slightly differently at Consumers vs
Producers:

Each Consumer node consumes from an audio stream that is conceptually always
running. When the Consumer is started, it joins at some place in the stream.
When the Consumer stops, then starts again, it must rejoin the stream at some
time after the position it stopped, because while it was stopped the stream was
still conceptually running in the background. This mimics real Consumer
scenarios, such as live microphones, which capture continuously in real time
even if we temporarily stop listening.

Producers don't necessarily represent a continuously advancing stream. Producers
may represent static streams that are being navigated dynamically by "loop" or
"seek" events, as in typical media players. When a Producer is started, it
starts producing from some point in the stream. When the Producer stops, then
restarts, it can restart at any media time, including at an older media time,
e.g. to implement a "seek backwards" action.

Hence, given a sequence *Start at (Tr0, Tm0), Stop at (Tr1, Tm1), Start at (Tr2,
Tm2)*, we always have *Tr0 ≤ Tr1 ≤ Tr2*, because reference time advances
continuously and monotonically. At Consumers, we have *Tm0 ≤ Tm1 ≤ Tm2*, while
at Producers we have *Tm0 ≤ Tm1* with no constraints *Tm2* because Producers
(unlike Consumers) are allowed to seek backwards.

## Translating between media time and frame time

At each Consumer and Producer, we define a translation between media time *Tm*
and frame time *Tf*. As above, we use a TimelineFunction with the following
values:

*   A pair of *(Tm0, Tf0)*, representing the epoch.
*   A rate *∆f / ∆m*, where every *∆m* steps of frame time correspond to *∆m*
    steps of media time.

For simplicity, the epoch is always *(0, 0)*, which means *Tf = Tm * ∆f / ∆m*.
When media time is expressed in units frames, then *∆f = ∆m* and media and frame
times are equivalent.

## Translating between frame time and presentation time

At each ConsumerStage and ProducerStage, we have translations from *Tf* to *Tm*
and *Tm* to *Tr* which can be computed into a translation from *Tf* to *Tr*. As
before, we say that *Tr* is the *presentation timestamp* for frame *Tf*.

As mentioned above, media time is defined at Consumers and Producers only, via
`Graph.Start` and `Graph.Stop` actions from clients. At Consumers and Producers,
we use media time to define frame time. All other PipelineStages operate
exclusively on frame time, which gets pushed "upwards" from Consumers. If the
mix graph uses a single clock and has no Splitters, then all PipelineStages
(except the Producers) will use the same frame timeline. In the general case,
frame timelines can change at Mixers (when a source stream uses a different
clock or frame rate than the destination stream) and at Splitters (which can
have multiple destination streams).

Each PipelineStage has a property `std::optional<TimelineFunction>
presentation_time_to_frac_frame` which holds the stage's current translation
from presentation time to frame time. On each call to `Graph.Start` or
`Graph.stop`, we compute the Consumer's `presentation_time_to_frac_frame`, as
described above, then pass that value up the tree by calling the following
recursive pseudocode:

```cpp
void UpdateFrameTimeline(std::optional<TimelineFunction> presentation_time_to_frac_frame) {
  if (this is a MixerStage) {
    this.presentation_time_to_frac_frame = presentation_time_to_frac_frame;
    for (auto source : this.sources) {
      if (source.reference_clock == this.reference_clock || !presentation_time_to_frac_frame) {
        source.UpdateFrameTimeline(presentation_time_to_frac_frame);
      } else {
        // When the clock changes, rebase the TimelineFunction. Given that
        // presentation_time_to_frac_frame has epoch (Tf, Tr) and rate ∆f/∆r,
        // compute the time Tr' which is equivalent to Tr in the source's
        // reference clock, then pass epoch (Tf, Tr') and rate ∆f/∆r upwards.
        auto new_f = presentation_time_to_frac_frame;
        new_f.reference_time = source.MonoTimeToRef(
             this.RefTimeToMono(presentation_time_to_frac_frame->reference_time));
        source.UpdateFrameTimeline(new_f);
    }
  }
  else if (this is Splitter) {
    // Initially, the SplitterStage is stopped. When the first destination stream
    // is started, we use that stream's TimelineFunction. We continue using that
    // TimelineFunction until all destinations are stopped, at which point the
    // Splitter also stops. When another destination is started, we use that
    // destination's TimelineFunction (with caveats below), and so on.
  }
  else if (this is Custom) {
    // If this stage has a single destination stream, pass presentation_time_to_frac_frame
    // to all sources. If the stage has multiple destination streams, treat this
    // like a Splitter.
  }
  else {
    // Must be a Producer. Stop recursing.
    // The Producer computes its own presentation_time_to_frac_frame on Stop/Start.
  }
}
```

Since all non-Producer stages get their frame timelines directly from Consumers,
and since Consumers' frame time cannot go backwards, then frame time cannot go
backwards at any non-Producer stage. The exception is when Splitters go through
a transition from Started, to Stopped, back to Started. When the Splitter
restarts, it may use the frame timeline of a different Consumer than when the
Splitter was first started. To account for this difference, the Splitter (and
all of its sources, recursively, not including Producers) must be
[`Reset`](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/media/audio/audio_core/stream.h;drc=6646d5fa6b2f5550f57912dd64b8c1c01039cf99;l=241)
to account for the fact that frame time may change in an arbitrary way. For a
similar reason, Producers must `Reset` each time they are restarted.

### Optimality

This algorithm upwards minimizes the number of frame timeline translations in
most cases. The exceptions involve Splitters: if the Splitter has destination
streams A and B, where A is started, then B is started, then A is stopped, then
while A is stopped, it would be optimal to switch the Splitter to use B's
timeline, but for simplicity we don't bother doing this.

### Concurrency

If the graph has no Splitters, `UpdateFrameTimeline` can run in an entirely
single-threaded way, since it touches only the set of nodes "owned" by the
Consumer which was started or stopped. If the graph has Splitters, then each
time we encounter a Splitter node we may need to hop to a different thread to
update the Splitter's source stream.

We run this algorithm directly on PipelineStages, starting from the Consumer
stage which was started or stopped. When we reach a ProducerStage which is a
child of a Splitter node, the ProducerStage sends a message to the Splitter's
ConsumerStage telling that stage to start (or stop) using the frame timeline
passed to UpdateFrameTimeline. With the Splitter's ConsumerStage receives this
message, it runs the algorithm summarized in UpdateFrameTimeline:

*   When the Splitter's ConsumerStage transitions from "stopped" to "started",
    it passes its frame timeline down to the Splitter's ProducerStage(s) in a
    "start" message. To ensure the producers are using the correct frame
    timeline before they start serving packets, this message is sent before any
    packets.

*   When the Splitter's ConsumerStage transitions from "started" to "stopped",
    it needs not do anything because all producers must already be stopped.

*   If a new destination stream is added to the Splitter while it is started,
    the new producer is given a "start" message before any packets.

## Other ideas

Another idea is to define one frame timeline per pair of (reference clock, frame
rate), where we always define `frame time 0 == presentation time 0`. All nodes
with the same clock and frame rate automatically have the same frame timeline.
This idea is dramatically simpler: every node uses the same frame time for
source and destination streams except for Mixer nodes, which may need a frame
time translation if the source and destination use different clocks or different
frame rates.

Unfortunately, this idea can cause a phase shift at Consumers by up to one
frame. We consider this precision insufficient. To see how this can happen,
consider a Consumer which represents an output device, where the output device
runs at frame rate 10kHz. Each frame is 100us. If the Consumer starts at *Tr =
150us*, then by the above simplified definitions, frame 1.5 must be the first
frame presented on at the device. Since output devices must consume whole
frames, the Consumer must round up or down, to frame 2.0 or 1.0. In the worst
case this leads to a phase shift of just less than one frame.

For output devices, *Tr* is selected by the hardware, so we have no control over
*Tr*. The only solution is to dynamically define *Tf* so it is integrally
aligned relative to the presentation time epoch *Tr*.
