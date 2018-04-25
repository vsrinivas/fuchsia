# Fuchsia In-Process Streaming Framework

This directory contains an in-process streaming framework used to implement
Fuchsia media components. It defines models for implementing media sources,
transforms and sinks (collectively called *nodes*) and connecting them into a
graph.

The framework addresses the following concerns:

- How multiple source, transforms and sinks (nodes) are assembled into a graph
- How memory that holds content is allocated
- How and when media moves from one node to another
- The threading model used execute node implementations
- How running graphs are modified

The framework *does not* address the following concerns:

- Compatibility of adjacent nodes with respect to media encoding (media type)
- Endpoint states and timing (start, stop etc)

## Graph

The `Graph` class is the primary entry point for the framework API. The client
instantiates a graph and uses its methods to add nodes to the graph and connect
the nodes together. The client then calls the `Graph::Prepare` method, at which
point the graph is ready to move packets.

Here's an example:

```
Graph graph(default_async);
NodeRef demux = graph.Add(demux_node);
NodeRef decoder = graph.Add(decoder_node);
NodeRef renderer = graph.Add(renderer_node);

graph.ConnectNodes(demux, decoder);
graph.ConnectNodes(decoder, renderer);

graph.Prepare();
```

The framework itself doesn't implement any of the nodes in the example, but it
does define the models used to write such nodes. It doesn't involve itself in
stream types. The demux, decoder and renderer in the example are assumed to be
compatible. It also doesn't get involved in timing or anything like play/pause
transitions. In the example above, the `renderer_node` would presumably support
some method of starting playback.

## Nodes

Nodes are the sources, sinks and transforms that produce, consume and transform
packets in the graph. Logically speaking, a particular node has zero or more
inputs and zero or more outputs.

An important design goal for the framework is to make nodes easy to write. Of
course, a decoder (for example) is a complicated thing, but the framework
minimizes the effort required to wrap a decoder as a node.

Rather than insisting that all
nodes use a uniform model, the framework supports multiple node models, each
model addressing a particular type of node. Currently there are five node
models. Sources, sinks and transforms have distinct models. Multi-stream and
single-stream sources and sinks are also given distinct models. Transforms
can be synchronous or asynchronous. Sources and sinks use asynchronous models,
given that synchronous source/sink operation is rare, and the asynchronous
models are easily applied to synchronous operation.

The five models currently supported are:

- `Source`: a source that produces a single stream of packets. Examples of nodes
  that might use this model are capture devices and bridges from other
  frameworks.
- `MultistreamSource`: a source that produces multiple streams of packets.
  The ffmpeg demux node uses this model.
- `Sink`: a sink that consumes a single stream of packets. Examples of nodes
  that might use this model are renderers and bridges to other frameworks.
- `MultistreamSink`: a sink that consumes multiple streams of packets. Examples
  of nodes that might use this model are a rendering audio mixer or a
  multiplexer.
- `Transform`: a transform that processes input packets and produces output
  packets on demand. Decoders can use this model. Other type conversion nodes
  and effects are also candidates.

Every model has its associated abstract base class and a corresponding *Stage*
class that models the node's host. The stage calls the methods defined in the
node's base class, and the node may call its stage to, for example, signal
external events.

## Threading Model

The framework is single-threaded. Individual nodes in a graph may employ
additional threads, if desired, but all calls in and out of the framework
use a single thread.

## Inputs, Output, Supply and Demand

The `Graph` object has various methods that connect outputs to inputs. Some
methods refer to outputs and inputs explicitly and others refer to them
implicitly for convenience.

For example, this code was presented earlier:

```
graph.ConnectNodes(demux, decoder);
```

`Graph::ConnectNodes` requires that the node referenced by the first parameter
have only one output and the the node referenced by the second parameter have
only one input. This code is really just shorthand for:

```
graph.Connect(demux.output(0), decoder.input(0));
```

Node models may refer to inputs and output explicitly, but more often, the
existence of inputs and outputs is implied by the model. The `Source`
model, for example, implies a single output, `Sink` a single input and
`Transform` one input and one output.

There are a number of *configuration* concerns surrounding connected
output/input pairs that we'll get into later. Primarily, though, connections
are about moving packets downstream (supply) and communicating the need for
packets (demand).

Supply and demand are communicated through a connected output/input pair using
a lockless scheme that allows adjacent nodes/stages to run concurrently without
interfering with each other. The framework also defines the notion of *updating*
a node/stage in the sense of responding to recent changes in availability of
packets or changes in demand for packets. These issues are largely the concern
of the stages and don't impinge much on the implementation of nodes.

## Allocators

TODO(dalesat)

## Programs and Program Ranges

TODO(dalesat)

## Dynamic Graph and Configuration Changes

TODO(dalesat)
