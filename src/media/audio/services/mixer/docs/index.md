# Audio Mixer Service

The MixerService implements the FIDL APIs under `fuchsia.audio.mixer`. The mixer
is a DAG. Audio flows from Producer nodes to Consumer nodes and, in between, may
flow through various processing nodes.

## Code layout

[`fidl/`](../fidl/) contains implementations of all FIDL protocols and all code
that manages the DAG of Nodes. Code in this directory runs on the
[FIDL thread](execution_model.md#concurrency).

[`mix/`](../mix/) defines all PipelineStage objects and all code which drives
the mix threads. Code in this directory cannot access FIDL servers or the DAG of
Nodes. Communication with other threads happens over thread-safe queues.

[`common/`](../common/) contains thread-safe code that can run on any thread.
Code in this directory must implement a thread-safe interface. For example, this
includes thread-safe queues and classes that accumulate metrics from multiple
threads.

## Naming patterns

Class names use the following patterns:

*   *Foo*Server is a server that implements FIDL protocol Foo. For example,
    GraphServer.

*   *Foo*Node is a DAG node of type Foo. All classes with this name are
    subclasses of [Node](../fidl/node.h).

*   *Foo*Stage is a processing stage in a pipeline tree. All classes with this
    name are subclasses of [PipelineStage](../mix/pipeline_stage.h).

## Detailed documentation

The following pages discuss specific topics in more detail:

*   [Execution model and concurrency rules](execution_model.md)
*   [Object lifetime and ownership](lifetime_and_ownership.md)
*   [Timelines](timelines.md)
*   [Clocks](clocks.md)
*   [Delay](delay.md)
*   [Splitter nodes](splitters.md)
