# Audio Mixer Service

The MixerService implements the FIDL APIs under `fuchsia.audio.mixer`. The mixer
is a DAG. Audio flows from Producer nodes to Consumer nodes and, in between, may
flow through various processing nodes.

## Code layout

[`fidl/`](../fidl/) contains implementations of all FIDL protocols and all code
that manages the DAG of Nodes.

[`mix/`](../mix/) defines all PipelineStage objects and all code which drives
the mix threads. This code is almost entirely single-threaded -- there is no
cross-thread communication with FIDL threads except for a few places that use
thread-safe queues (see [concurrency](execution_model.md#concurrency) for more
details).

[`common/`](../common/) contains thread-safe code that can run on any thread.
Code in this directory must implement a thread-safe interface. For example, this
includes thread-safe queues and classes that accumulate metrics from multiple
threads.

## Naming patterns

Class names use the following patterns:

*   Fidl*Foo* is a server that implements FIDL protocol Foo. For example,
    FidlGraph.

*   *Foo*Node is a DAG node of type Foo. All classes with this name are
    subclasses of [Node](../fidl/node.h).

*   *Foo*Stage is a processing stage in a pipeline tree. All classes with this
    name are subclasses of [PipelineStage](../mix/pipeline_stage.h).

## Detailed documentation

The following pages discuss specific topics in more detail:

*   [Execution model and concurrency rules](execution_model.md)
*   [Object lifetime and ownership](lifetime_and_ownership.md)
*   [Timelines](timelines.md)
*   [Delay](delay.md)
*   [Clocks](clocks.md)
