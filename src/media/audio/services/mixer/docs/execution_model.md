# Audio Mixer Service: Execution model and concurrency rules

[TOC]

## Execution model

Execution is driven by Consumers. Each Consumer performs a sequence of mix jobs,
where each mix job produces N ms of audio. Mix jobs are run periodically: every
N ms, the Consumer wakes and pulls N ms of audio from its inputs, which pulls
audio from their inputs, and so on, up to the connected Producers. As audio is
pulled down the graph, it is processed and mixed into a single stream.

We partition the DAG so that each Consumer is the root of a tree: at nodes with
multiple outputs, such as Splitters, we write the node's input to a temporary
buffer and create a hidden Producer node for each output. This is implemented
using *meta nodes*, as illustrated in [fidl/node.h](../fidl/node.h) and
[fidl/splitter_node.h](../fidl/splitter_node.h).

## Threads

The service uses three kinds of threads:

*   The "FIDL thread" is responsible for all graph operations for protocols such
    as `fuchsia.audio.mixer.GraphFactory`, `fuchsia.audio.mixer.Graph`,
    `fuchsia.audio.GainControl` and `fuchsia.media2.StreamSink`.

*   The "mix threads" are responsible for all audio processing. Each Consumer is
    assigned to a mix thread (the steps described under "Execution model" happen
    on this thread). The number of mix threads is defined by the client. In
    general we may have multiple Consumers assigned to each mix thread.

## Concurrency {#concurrency}

To avoid races between graph updates (from the FIDL thread) and mix jobs (from
the mix threads), we maintain *Nodes* separately from *PipelineStages*:

*   The FIDL thread owns a DAG of [Nodes](../fidl/node.h). Nodes are created by
    methods in `fuchsia.audio.mixer.Graph`, such as `CreateProducer`, and linked
    and unlinked with `CreateEdge` and `DeleteEdge`.

*   The mix threads control a set of *pipeline trees* composed of
    [PipelineStage](../mix/pipeline_stage.h) objects. For each Node there is a
    corresponding PipelineStage type (ConsumerStage, MixerStage, and so on). The
    root of each pipeline tree is a ConsumerStage object. Mix jobs walk these
    pipeline trees to produce output at each Consumer.

    Edges between PipelineStage objects flow from destination to source. For
    example, the edge `A -> B` says that A reads audio from from B. If there is
    a path from ConsumerStage C to PipelineStage object O, then object O is said
    to be *assigned* to the same mix thread as C. Otherwise, O is said to be
    *detached*.

Pipeline trees form a shadow underneath the DAG of Nodes. Within a DAG there can
be multiple pipeline trees. Nodes maintain pointers to PipelineStage objects,
but there are no pointers in the other direction.

When the FIDL thread receives a DAG update, such as `CreateEdge` or
`DeleteNode`, the DAG is updated immediately. Since the DAG is fully managed by
the FIDL thread, this update can happen in a purely-single-threaded way.
Pipeline trees are updated asynchronously, with *eventually consistent*
semantics, meaning that pipeline trees will eventually contain the same
structure as the DAG, once all pending updates are applied. This happens using
queues, as explained below.

### Updates to the DAG structure

A challenge is that structural updates must happen in a sequential order, but
individual updates must happen on specific threads. For example, the pipeline
edge `A->B` must be created by the mix thread assigned to `A`, although if `A`
is detached, then this edge can be created by any mix thread.

We use a thread-safe [global task queue](common/global_task_queue.h), where each
entry in the task queue is a pair of `(ThreadId, task)`. The ThreadId specifies
which thread the task must be performed on (or uses `kAnyThreadId` if the task
can be performed on any thread). When a mix thread has time to spare, it runs
all tasks at the head of the global task queue until it reaches a task that must
be run by a different thread. The following example shows how FIDL calls
generate tasks for this queue:

```
Suppose X is assigned to mix thread T1
Suppose Y is assigned to mix thread T2
Suppose A,B,C,D are detached

CreateEdge(dest=X, source=A)  // push (T1, AddSource(dest=X, source=A))
CreateEdge(dest=X, source=B)  // push (T1, AddSource(dest=X, source=B))
DeleteEdge(dest=X, source=A)  // push (T1, RemoveSource(dest=X, source=A))
CreateEdge(dest=A, source=C)  // push (Invalid, AddSource(dest=A, source=C))
CreateEdge(dest=A, source=D)  // push (Invalid, AddSource(dest=A, source=D))
CreateEdge(dest=Y, source=A)  // push (T2, AddSource(dest=Y, source=A))
```

In the above example, `AddSource(dest, source)` creates an edge `dest -> source`
in the pipeline forest, while `RemoveSource` removes that edge. The first three
calls must happen on thread `T1` because `X` is assigned to `T1`. The fourth and
fifth calls may happen on any thread because `A` is detached. It's even ok if
these calls happen on different threads, since the calls are serialized by the
global task queue. The final call must happen on thread `T2`.

### Updates to other state

For state that doesn't affect the DAG structure and doesn't need to be
serialized with structural updates, we use separate thread-safe queues. This
avoids global serialization, which is especially important for latency-sensitive
operations. Currently we use the following additional queues:

*   When a Producer is backed by a StreamSink, the FIDL thread pushes packets
    onto a queue as those packets arrive. This queue is shared by a
    PacketQueueProducerStage object, which consumes packets from the queue as
    mix jobs are executed.
