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

### Output pipelines

In output pipelines, a.k.a. render pipelines, each producer stage has a *lead
time* which is maximum of the total delays for all paths from the producer to
all output-mode consumers (typically analog speakers). For example, if a
producer feeds into both a USB speaker and a BT speaker, the producer's lead
time is the maximum of the path-to-USB delay and the path-to-BT delay. Clients
must submit audio frame X ahead of X's presentation time by at least the *lead
time*, otherwise frame X may underflow.

### Input pipelines

In input pipelines, a.k.a. capture pipelines, every input-mode consumer has a
delay which is the maximum delay on any path from an input-mode producer
(typically an analog microphone) to the consumer. For example, if the client is
reading from a consumer that is fed by two producers (perhaps a microphone and a
loopback interface), the consumer's total delay is the maximum of the
delay-from-microphone and the delay-from-loopback.

This delay represents the earliest time a client can read a frame. For example,
if a consumer's total delay is 20ms, then a frame which reaches a microphone at
time T cannot be read by a client until time T+20ms.

### Delay in mixed pipelines

An output pipeline might feed an input pipeline through a loopback interface.
When this happens, the loopback interface must appear somewhere before the
output pipeline's final consumers, which implies that the pipeline must write
frame X to the loopback interface well before frame X must be presented at a
speaker. Hence, the *delay* at the loopback interface is zero.

It can be useful to feed an input pipeline into an output pipeline. For example,
an application might add effects to live music in real time, where the music is
captured by a microphone then music plus effects are rendered to a speaker.
However, it is illegal to do make this connection within a MixerService DAG.
Suppose we tried to feed an input pipeline into an output pipeline, where the
input pipeline has delay D. If frame X is presented at a microphone at
presentation time T, X cannot be read until time T+D, at which point it is
inserted into the output pipeline. This frame has presentation time T, so by
definition it arrives late in the output pipeline. Any connection from an input
pipeline to an output pipeline must occur in an external client, which must
re-timestamp captured audio frames before they are fed into an output pipeline.
For example, if the input pipeline has delay D1, the output pipeline has delay
D2, and the client's internal processing has delay D3, then frame X should be
re-timestamped to T+D1+D2+D3.

### Reporting changes in lead time

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
producer needs to know how long that packet may be held by the mixer service. In
theory, the packet can be released at time T-LeadTime, where *T* is the
presentation time of the packet's last frame and *LeadTime* is the producer's
lead time. If this happened, the producer would not need to buffer more than one
packet at a time. However, the mixer service would need to immediately copy the
packet into an internal buffer.

At the other extreme, the mixer service might hold onto a packet beyond time T:
it may need a source frame up until that frame is presented, then may need to
hold onto the source frame a little longer, if, for example, the pipeline
includes a filter that looks into the past by a non-zero number of frames.

In practice, most pipeline trees use internal caches, so they will release a
frame well before the frame's presentation time. Computing exactly when this
happens can be tricky as it depends on the exact semantics of each pipeline
stage. To avoid this problem, we impose the following simple rules:

*   Each producer must be prepared to buffer up to one *lead time* worth of
    data.

*   If a pipeline stage includes a filter that looks into the past, the stage
    must cache frames in the past -- it cannot require its source to hold onto
    those frames.

TODO(fxbug.dev/87651): how does this affect ring buffers?
