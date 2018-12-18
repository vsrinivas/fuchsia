# Fuchsia In-Process Streaming Framework

This directory contains an in-process streaming framework used to implement
Fuchsia media components. It defines a model for implementing media sources,
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
the nodes together. At this point, the graph is ready to move packets.

Here's an example:

```
Graph graph(default_async);
NodeRef demux = graph.Add(demux_node);
NodeRef decoder = graph.Add(decoder_node);
NodeRef renderer = graph.Add(renderer_node);

graph.ConnectNodes(demux, decoder);
graph.ConnectNodes(decoder, renderer);
```

The framework itself doesn't implement any of the nodes in the example, but it
does define the model used to write such nodes. It doesn't involve itself in
stream types. The demux, decoder and renderer in the example are assumed to be
compatible. It also doesn't get involved in timing or anything like play/pause
transitions. In the example above, the `renderer_node` would presumably support
some method of starting playback.

## Nodes

Nodes are the sources, sinks and transforms that produce, consume and transform
packets in the graph. Logically speaking, a particular node has zero or more
inputs and zero or more outputs.

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

There are a number of *configuration* concerns surrounding connected
output/input pairs that we'll get into later. Primarily, though, connections
are about moving packets downstream (supply) and communicating the need for
packets (demand).

Supply and demand are communicated through a connected output/input pair using
a lockless scheme that allows adjacent nodes to run concurrently without
interfering with each other. The framework also defines the notion of *updating*
a node in the sense of responding to recent changes in availability of
packets or changes in demand for packets. These issues are largely the concern
of the nodes and don't impinge much on the implementation of nodes.

## Allocators

TODO(dalesat)

## Programs and Program Ranges

TODO(dalesat)

## Dynamic Graph and Configuration Changes

TODO(dalesat)
