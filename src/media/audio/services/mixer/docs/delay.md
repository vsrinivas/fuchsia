# Audio Mixer Service: Delay

[TOC]

## Delay

In output pipelines, frames must be presented on time, otherwise the frame will
underflow, which can manifest as an audible dropout or glitch. Each frame must
go through some amount of processing before it can be presented. This processing
time is known as *delay*. Hence, in order for a frame to be presented on time,
it must enter the output pipeline by its
[*presentation timestamp*](timelines.md) minus the pipeline's *delay*.

Delay can be introduced by any stage of an audio pipeline, from three sources:

1.  **Lookahead**. Some audio processors, such as resamplers, compute each
    destination frame as a function over a window of source frames. This window
    is centered around a source frame, where the window looks into the past by a
    *negative filter length* and into the future by a *positive filter length*.

    When an audio processor has a non-zero lookahead P, the output is
    effectively delayed by P frames: to produce N frames of output, we must give
    the processor N+P frames of input.

2.  **Block size**. Some audio processors operate on blocks of data at one time.
    For example, if a ConsumerStage runs a mix job every 10ms and produces 10ms
    of audio every 10ms, then the ConsumerStage has a block size of 10ms.

    When an audio processor has a block size of B frames, where B > 1, the
    processor has a delay of B-1 frames, because to produce the first frame of
    any block, the processor needs B-1 additional frames to complete the block.

3.  **Physical delay**. Every audio processor introduces a physical delay, which
    can drive from CPU time needed to compute the output, or from the physical
    time needed for a signal to transit from a digital interface to an analog
    speaker.

Our [pipeline stages](execution_model.md) can introduce all three kinds of
delays. For example, a MixerStage that needs to perform rate conversion may have
a non-zero lookahead. A CustomStage may have both a lookahead and a block size,
depending on the specific effect implemented at that CustomStage. A
ConsumerStage has a block size (derived from the mix period) and a physical
delay (derived from the total CPU time needed to run all processors in the
consumer's [pipeline tree](execution_model.md)).

Given any path between two pipeline stages, the path's *total delay* is given by
the following equation, where *i* refers to pipeline stage *i*:

```
sum(lookahead[i]) + sum(physical_delay[i]) + sum(block_size[i]-1)
```

Note that the last expression can be `lcm(block_size[i])-1`, where `lcm`
computes the least-common multiple, but this requires carefully synchronizing
all pipeline stages and is useful only when each `block_size[i]` is a divisor of
`max(block_size[i])`.

### Downstream vs upstream delays

In a mix graph, every edge has a delay which represents the buffering and
processing performed by the edge's destination node. At any node, we can compute
delay in two directions: *downstream* delay is the maximum delay on any path
through the node's outgoing edges, while *upstream* delay is the maximum delay
on any path through the node's incoming edges. Note that a node's *upstream*
delay includes the delay introduced by that node (via the delay on incoming
edges), while *downstream* delay does not.

### Output pipelines

In output pipelines, a.k.a. render pipelines, the most important delay is the
*downstream* delay, which is the maximum the delay over all paths leading to an
output device (typically a speaker) or a loopback interface. This delay is also
known as the renderer's *lead time*. If a client wants to play a frame at time
T, and the lead time is 20ms, the client must submit that frame to the renderer
by time T-20ms.

### Input pipelines

In input pipelines, a.k.a. capture pipelines, the most important delay is the
*upstream* delay, which is the maximum delay over all paths originating from an
input device (typically a microphone). This delay controls the earliest time a
client can read a frame: if a capturer's upstream delay is 20ms, then a frame
which reaches a microphone at time T cannot be read by a client until time
T+20ms.

### Loopback pipelines

An output pipeline might feed into an input pipeline through a loopback
interface. Loopback interfaces have zero delay: if a frame is presented at time
T, it must arrive at the loopback interface by T, making it immediately
available to the input pipeline at T.

### Illegal pipelines

It might be useful to feed an input pipeline into an output pipeline, but we do
not allow it.

For example, an application might capture live music from a microphone, add
effects in real time, then render the music plus effects to a speaker. However,
we forbid connections of this type. Suppose we tried to feed an input pipeline
into an output pipeline, where the input pipeline has delay D. If frame X is
presented at a microphone at time T, X cannot be read until time T+D, at which
point it is inserted into the output pipeline. This frame has presentation time
T, so by definition it arrives late in the output pipeline, hence the frame will
not be rendered. Scenarioes like this must route the input pipeline to an
external client, which must re-timestamp captured audio frames before they are
fed into an output pipeline. For example, if the input pipeline has delay D1,
the output pipeline has delay D2, and the client's internal processing has delay
D3, then frame X should be re-timestamped to T+D1+D2+D3.

## Methods on Node

The [Node class](../fidl/node.h) defines the following methods:

*   `max_downstream_output_pipeline_delay()`
*   `max_downstream_input_pipeline_delay()`
*   `max_upstream_input_pipeline_delay()`

We do not bother to define `max_upstream_output_pipeline_delay()` because (so
far) that is not useful. The above delays measure *downstream* or *upstream*
delay, filtered by output or input pipelines, as illustrated by the following
diagram:

```
 +  producer                        + microphone
 |     |                            |     |
 |     V                            |     V
 |    ...                           |  producer
 |     |                            |     |
 |     V                           (c)    V
 |  loopback -> ... -> consumer     |    ...
 |     +--------(b)-------+         |     |
(a)    |                            |     V
 |     V                            +  consumer
 |  consumer
 |     |
 |     V
 +  speaker
```

*   The delay on path (a) is `producer.max_downstream_output_pipeline_delay()`
*   The delay on path (b) is both
    `producer.max_downstream_input_pipeline_delay()` and
    `loopback.max_downstream_input_pipeline_delay()`
*   The delay on path (c) is `consumer.max_upstream_input_pipeline_delay()`

These delays are used as described in the following sections.

### ProducerNode

In output pipelines, `max_downstream_output_pipeline_delay()` is the producer's
lead time.

In input pipelines, `max_upstream_input_pipeline_delay()` is the delay
introduced by the external input device on the other side of the producer node
(e.g. a microphone device).

### ConsumerNode

In output pipelines, `max_downstream_output_pipeline_delay()` is the delay
introduced by the external output device on the other side of the consumer node
(e.g. a speaker device).

In input pipelines, `max_upstream_input_pipeline_delay()` is how long it takes
for a frame to travel from the external input device (e.g. a microphone) to the
consumer.

### SplitterNode

A splitter node copies a source stream into an intermediate buffer, which is
read by multiple destination streams.

When a splitter node appears in an output pipeline, the buffer must be large
enough to accommodate the splitter's maximum *downstream* delay, which is
`max_downstream_output_pipeline_delay()`.

When a splitter node appears in an input pipeline, the buffer must be large
enough to accommodate the splitter's maximum *downstream* delay, which is
`max_downstream_input_pipeline_delay()`.

When a splitter node acts as a loopback interface, meaning the splitter's source
is an output pipeline and at least one destination feeds into an input pipeline,
the intermediate buffer is split into two parts: one for the downstream output
pipelines (which process frames that will be presented in the future) and
another for the downstream input pipelines (which read frames that were
presented in the past). The intermediate buffer must be large enough for
`max_downstream_output_pipeline_delay() + max_downstream_input_pipeline_delay()`
frames.

## Reporting changes in lead time

The client must be notified when an output pipeline's *lead time* changes.
Suppose the lead time is scheduled to change at time T. If the lead time
decreases by duration D, the client can be notified at any time. It's always
safe for the client to believe that the lead time is larger than it actually is.

If the lead time increases by duration D, the client must be notified by time
T-D at the latest to avoid underflow, though in practice the client may need to
be notified earlier. For example, if the client generates audio in 10 ms
batches, where each batch takes 2 ms to generate, then the client needs to be
notified by time $$T - D - (2ms) \cdot ceil(D/10ms)$$.

TODO(fxbug.dev/87651): design an API for this

## Buffer size

When a producer submits audio into a pipeline tree using a packet queue, the
producer needs to know how long that packet may be held by the mixer service. At
one extreme, the mixer service might immediately release the packet after coying
it into an internal cache. At another extreme, a mixer pipeline might have an
arbitrarily-large look-behind, in which case a packet might be needed for an
arbitrarily long time.

To simplify, we impose the following rules:

*   In output pipelines, the producer must be prepared to buffer at least
    `producer.max_downstream_output_pipeline_delay()`, i.e. at least one *lead
    time* worth of data.

*   In input pipelines, the producer must be prepared to buffer at least
    `producer.max_downstream_input_pipeline_delay()` worth of data.

*   If a pipeline stage includes a filter with look-behind, the stage must cache
    frames in the past -- it cannot require its source to hold onto those
    frames.
