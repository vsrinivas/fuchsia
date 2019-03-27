// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/graph/graph.h"

#include "src/media/playback/mediaplayer_tmp/graph/formatting.h"
#include "src/media/playback/mediaplayer_tmp/util/callback_joiner.h"
#include "src/media/playback/mediaplayer_tmp/util/threadsafe_callback_joiner.h"

namespace media_player {

Graph::Graph(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

Graph::~Graph() { Reset(); }

NodeRef Graph::Add(std::shared_ptr<Node> node) {
  FXL_DCHECK(node);
  FXL_DCHECK(dispatcher_);

  node->SetDispatcher(dispatcher_);
  node->ConfigureConnectors();

  nodes_.push_back(node);

  if (node->input_count() == 0) {
    sources_.push_back(node.get());
  }

  if (node->output_count() == 0) {
    sinks_.push_back(node.get());
  }

  return NodeRef(node.get());
}

void Graph::RemoveNode(NodeRef node_ref) {
  FXL_DCHECK(node_ref);

  Node* node = node_ref.node_;

  size_t input_count = node->input_count();
  for (size_t input_index = 0; input_index < input_count; input_index++) {
    Input& input = node->input(input_index);
    if (input.connected()) {
      DisconnectInput(InputRef(&input));
    }
  }

  size_t output_count = node->output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    Output& output = node->output(output_index);
    if (output.connected()) {
      DisconnectOutput(OutputRef(&output));
    }
  }

  sources_.remove(node);
  sinks_.remove(node);
  nodes_.remove(node->shared_from_this());
}

NodeRef Graph::Connect(const OutputRef& output_ref, const InputRef& input_ref) {
  FXL_DCHECK(output_ref);
  FXL_DCHECK(input_ref);

  if (output_ref.connected()) {
    DisconnectOutput(output_ref);
  }
  if (input_ref.connected()) {
    DisconnectInput(input_ref);
  }

  Output& output = *output_ref.actual();
  Input& input = *input_ref.actual();

  input.Connect(&output);

  // This call might apply the output configuration to the payload manager.
  output.Connect(&input);

  // If the payload manager is ready, notify the nodes.
  if (input.payload_manager().ready()) {
    input.node()->NotifyInputConnectionReady(input.index());
    output.node()->NotifyOutputConnectionReady(output.index());
  }

  return input_ref.node();
}

NodeRef Graph::ConnectNodes(NodeRef upstream_node, NodeRef downstream_node) {
  FXL_DCHECK(upstream_node);
  FXL_DCHECK(downstream_node);
  Connect(upstream_node.output(), downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectOutputToNode(const OutputRef& output,
                                   NodeRef downstream_node) {
  FXL_DCHECK(output);
  FXL_DCHECK(downstream_node);
  Connect(output, downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectNodeToInput(NodeRef upstream_node,
                                  const InputRef& input) {
  FXL_DCHECK(upstream_node);
  FXL_DCHECK(input);
  Connect(upstream_node.output(), input);
  return input.node();
}

void Graph::DisconnectOutput(const OutputRef& output) {
  FXL_DCHECK(output);

  if (!output.connected()) {
    return;
  }

  Output* actual_output = output.actual();
  FXL_DCHECK(actual_output);
  Input* mate = actual_output->mate();
  FXL_DCHECK(mate);

  mate->Disconnect();
  actual_output->Disconnect();
}

void Graph::DisconnectInput(const InputRef& input) {
  FXL_DCHECK(input);

  if (!input.connected()) {
    return;
  }

  Input* actual_input = input.actual();
  FXL_DCHECK(actual_input);
  Output* mate = actual_input->mate();
  FXL_DCHECK(mate);

  mate->Disconnect();
  actual_input->Disconnect();
}

void Graph::RemoveNodesConnectedToNode(NodeRef node) {
  FXL_DCHECK(node);

  std::deque<NodeRef> to_remove{node};

  while (!to_remove.empty()) {
    NodeRef node = to_remove.front();
    to_remove.pop_front();

    for (size_t i = 0; i < node.input_count(); ++i) {
      if (node.input(i).connected()) {
        to_remove.push_back(node.input(i).mate().node());
      }
    }

    for (size_t i = 0; i < node.output_count(); ++i) {
      if (node.output(i).connected()) {
        to_remove.push_back(node.output(i).mate().node());
      }
    }

    RemoveNode(node);
  }
}

void Graph::RemoveNodesConnectedToOutput(const OutputRef& output) {
  FXL_DCHECK(output);

  if (!output.connected()) {
    return;
  }

  NodeRef downstream_node = output.mate().node();
  DisconnectOutput(output);
  RemoveNodesConnectedToNode(downstream_node);
}

void Graph::RemoveNodesConnectedToInput(const InputRef& input) {
  FXL_DCHECK(input);

  if (!input.connected()) {
    return;
  }

  NodeRef upstream_node = input.mate().node();
  DisconnectInput(input);
  RemoveNodesConnectedToNode(upstream_node);
}

void Graph::Reset() {
  sources_.clear();
  sinks_.clear();

  auto joiner = ThreadsafeCallbackJoiner::Create();

  for (auto& node : nodes_) {
    node->Acquire(joiner->NewCallback());
  }

  joiner->WhenJoined(dispatcher_, [nodes = std::move(nodes_)]() mutable {
    while (!nodes.empty()) {
      std::shared_ptr<Node> node = nodes.front();
      nodes.pop_front();
      node->ShutDown();
    }
  });
}

void Graph::FlushOutput(const OutputRef& output, bool hold_frame,
                        fit::closure callback) {
  FXL_DCHECK(output);
  std::queue<Output*> backlog;
  backlog.push(output.actual());
  FlushOutputs(&backlog, hold_frame, std::move(callback));
}

void Graph::FlushAllOutputs(NodeRef node, bool hold_frame,
                            fit::closure callback) {
  FXL_DCHECK(node);

  std::queue<Output*> backlog;
  size_t output_count = node.output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    backlog.push(node.output(output_index).actual());
  }

  FlushOutputs(&backlog, hold_frame, std::move(callback));
}

void Graph::PostTask(fit::closure task,
                     std::initializer_list<NodeRef> node_refs) {
  auto joiner = ThreadsafeCallbackJoiner::Create();

  std::vector<Node*> nodes;
  for (NodeRef node_ref : node_refs) {
    node_ref.node_->Acquire(joiner->NewCallback());
    nodes.push_back(node_ref.node_);
  }

  joiner->WhenJoined(dispatcher_,
                     [task = std::move(task), nodes = std::move(nodes)]() {
                       task();
                       for (auto node : nodes) {
                         node->Release();
                       }
                     });
}

void Graph::FlushOutputs(std::queue<Output*>* backlog, bool hold_frame,
                         fit::closure callback) {
  FXL_DCHECK(backlog);

  auto callback_joiner = CallbackJoiner::Create();

  // Walk the graph downstream from the outputs already in the backlog until
  // we hit a sink. The |FlushOutputExternal| and |FlushInputExternal| calls are
  // all issued synchronously from this loop, and then we wait for all the
  // callbacks to be called. This works, because downstream flow is halted
  // synchronously, even though the nodes may have additional flushing business
  // that needs time to complete.
  while (!backlog->empty()) {
    Output* output = backlog->front();
    backlog->pop();
    FXL_DCHECK(output);

    if (!output->connected()) {
      continue;
    }

    Input* input = output->mate();
    FXL_DCHECK(input);
    Node* input_node = input->node();

    output->node()->FlushOutputExternal(output->index(),
                                        callback_joiner->NewCallback());

    input_node->FlushInputExternal(input->index(), hold_frame,
                                   callback_joiner->NewCallback());

    for (size_t output_index = 0; output_index < input_node->output_count();
         ++output_index) {
      backlog->push(&input_node->output(output_index));
    }
  }

  callback_joiner->WhenJoined(std::move(callback));
}

void Graph::VisitUpstream(Input* input, const Visitor& visitor) {
  FXL_DCHECK(input);

  std::queue<Input*> backlog;
  backlog.push(input);

  while (!backlog.empty()) {
    Input* input = backlog.front();
    backlog.pop();
    FXL_DCHECK(input);

    if (!input->connected()) {
      continue;
    }

    Output* output = input->mate();
    Node* output_node = output->node();

    visitor(input, output);

    for (size_t input_index = 0; input_index < output_node->input_count();
         ++input_index) {
      backlog.push(&output_node->input(input_index));
    }
  }
}

}  // namespace media_player
