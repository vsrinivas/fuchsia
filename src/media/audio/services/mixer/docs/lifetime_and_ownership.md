# Audio Mixer Service: Object lifetime and ownership

We use `shared_ptr` everywhere so that use-after-free bugs manifest as memory
leaks instead of undefined behavior. Also, `shared_ptr` is a natural fit for
*eventually consistent* data structures (see our
[concurrency rules](execution_model.md#concurrency)) where, by design, there is
no synchronous waiting across threads. If an object is managed by a
`shared_ptr`, its destructor must:

1.  *Eventually destroy references to owned objects*: Despite using
    `shared_ptr`, we have a logical ownership tree. If object A owns object B,
    then A's destructor must ensure that all references to B are eventually
    removed so that B will be eventually destroyed as well.

2.  *Be side-effect free*: Using `shared_ptr` makes is difficult to reason about
    destruction order. To avoid unexpected behavior, destructors should not have
    side effects except to unlink owned objects as mentioned above.

Top-level FIDL servers (FidlGraphCreator and FidlGraph) are owned by FIDL
channels. A server is automatically destroyed when its underlying FIDL channel
is closed. Each FidlGraph server owns all objects created by a `Graph.CreateX`
call, including DAG Nodes, Threads, and GainControls. Lifetimes are fully
controlled by CreateX and DeleteX FIDL calls -- although we use `shared_ptr` to
avoid accidental use-after-free bugs, there is no strict need for reference
counting. These objects are fully reclaimed when the FidlGraph server is
destroyed. Most other objects are directly owned by one of these objects. Key
parts of FidlGraphs' ownership tree are summarized below:

*   FidlGraph

    *   ProducerNode

        *   if packet-driven:

            *   `fuchsia.media2.StreamSink` channel
                *   FidlStreamSink
            *   PacketQueueProducerStage

        *   if ring-buffer-driven:

            *   RingBuffer
            *   RingBufferProducerStage

    *   ConsumerNode

        *   if packet-driven:
            *   `fuchsia.media2.StreamSink` channel
        *   if ring-buffer-driven:
            *   RingBuffer

    *   MixerNode

        *   MixerStage

    *   SplitterNode

        *   ConsumerNode (for the input)
        *   ProducerNodes (for the outputs)

    *   GraphMixThread

        *   underlying kernel thread

Lastly, objects which live until the program exits are owned by a special
Globals object.
