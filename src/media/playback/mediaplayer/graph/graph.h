// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_GRAPH_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_GRAPH_H_

#include <lib/fit/function.h>

#include <list>

#include "src/media/playback/mediaplayer/graph/nodes/node.h"
#include "src/media/playback/mediaplayer/graph/refs.h"

namespace media_player {

//
// USAGE
//
// Graph is a container for sources, sinks and transforms ('nodes') connected
// in a graph. NodeRef, InputRef and OutputRef are all
// references to nodes and their inputs and outputs. Graph provides a variety
// of methods for adding and removing nodes and for connecting inputs and
// outputs to form a graph.
//
// The graph isn't thread-safe. If the graph is to be modified and/or
// interrogated on multiple threads, the caller must provide its own lock
// to prevent collisions. In this case, the caller must also acquire the same
// lock when making calls that cause nodes to add or remove inputs or outputs.
//
// Nodes added to the graph are referenced using shared pointers. The graph
// holds pointers to the nodes it contains, and the application, in many cases,
// also holds pointers to the nodes so it can call methods that are outside the
// graph's scope. When a node is added, the graph returns a NodeRef
// object, which can be used to reference the node when the graph is modified.
// NodeRef objects can be interrogated to retrieve inputs (as
// InputRef objects) and outputs (as OutputRef objects).
//
// Nodes come in various flavors, defined by 'model' abstract classes.
//

//
// DESIGN
//
// The Graph is implemented as a system of cooperating objects. Of those
// objects, only the graph itself is of relevance to code that uses Graph and
// to node implementations. The other objects are:
//
// Input
// A node possesses zero or more Input instances. Input objects
// implement the supply of media into the node and demand for media signalled
// upstream. Inputs recieve media from Outputs in the form of packets
// (type Packet).
//
// Output
// A node possesses zero or more Output instances. Output objects
// implement the supply of media output of the node to a downstream input and
// demand for media signalled from that input.
//

// Host for a source, sink or transform.
class Graph {
 public:
  // Constructs a graph.
  Graph(async_dispatcher_t* dispatcher);

  ~Graph();

  // Adds a node to the graph.
  NodeRef Add(std::shared_ptr<Node> node);

  // Removes a node from the graph after disconnecting it from other nodes.
  void RemoveNode(NodeRef node);

  // Connects an output connector to an input connector. Returns the dowstream
  // node.
  NodeRef Connect(const OutputRef& output, const InputRef& input);

  // Connects a node with exactly one output to a node with exactly one input.
  // Returns the downstream node.
  NodeRef ConnectNodes(NodeRef upstream_node, NodeRef downstream_node);

  // Connects an output connector to a node that has exactly one input. Returns
  // the downstream node.
  NodeRef ConnectOutputToNode(const OutputRef& output, NodeRef downstream_node);

  // Connects a node with exactly one output to an input connector. Returns the
  // downstream node.
  NodeRef ConnectNodeToInput(NodeRef upstream_node, const InputRef& input);

  // Disconnects an output connector and the input connector to which it's
  // connected.
  void DisconnectOutput(const OutputRef& output);

  // Disconnects an input connector and the output connector to which it's
  // connected.
  void DisconnectInput(const InputRef& input);

  // Disconnects and removes node and everything connected to it.
  void RemoveNodesConnectedToNode(NodeRef node);

  // Disconnects and removes everything connected to output.
  void RemoveNodesConnectedToOutput(const OutputRef& output);

  // Disconnects and removes everything connected to input.
  void RemoveNodesConnectedToInput(const InputRef& input);

  // Removes all nodes from the graph.
  void Reset();

  // Flushes the output and the subgraph downstream of it. |hold_frame|
  // indicates whether a video renderer should hold and display the newest
  // frame. |callback| is called when all flushes are complete.
  void FlushOutput(const OutputRef& output, bool hold_frame,
                   fit::closure callback);

  // Flushes the node and the subgraph downstream of it. |hold_frame|
  // indicates whether a video renderer should hold and display the newest
  // frame. |callback| is called when all flushes are complete.
  void FlushAllOutputs(NodeRef node, bool hold_frame, fit::closure callback);

  // Executes |task| after having acquired |nodes|. No update or other
  // task will touch any of the nodes while |task| is executing.
  void PostTask(fit::closure task, std::initializer_list<NodeRef> nodes);

 private:
  using Visitor = fit::function<void(Input* input, Output* output)>;

  // Flushes all the outputs in |backlog| and all inputs/outputs downstream
  // and calls |callback| when all flush operations are complete. |backlog| is
  // empty when this method returns.
  void FlushOutputs(std::queue<Output*>* backlog, bool hold_frame,
                    fit::closure callback);

  // Flushes the output and the subgraph downstream of it. |hold_frame|
  // indicates whether a video renderer should hold and display the newest
  // frame. |callback| is used to signal completion.
  void FlushOutput(Output* output, bool hold_frame, fit::closure callback);

  // Visits |input| and all inputs upstream of it (breadth first), calling
  // |visitor| for each connected input.
  void VisitUpstream(Input* input, const Visitor& visitor);

  async_dispatcher_t* dispatcher_;

  std::list<std::shared_ptr<Node>> nodes_;
  std::list<Node*> sources_;
  std::list<Node*> sinks_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_GRAPH_H_
