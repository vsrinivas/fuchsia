// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/graph.h"

namespace media {

Graph::Graph() {
  update_function_ = [this](Stage* stage) { engine_.RequestUpdate(stage); };
}

Graph::~Graph() {
  Reset();
}

void Graph::RemoveNode(NodeRef node) {
  FTL_DCHECK(node.valid());

  Stage* stage = node.stage_;

  size_t input_count = stage->input_count();
  for (size_t input_index = 0; input_index < input_count; input_index++) {
    if (stage->input(input_index).connected()) {
      DisconnectInput(InputRef(stage, input_index));
    }
  }

  size_t output_count = stage->output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    if (stage->output(output_index).connected()) {
      DisconnectOutput(OutputRef(stage, output_index));
    }
  }

  stage->SetUpdateCallback(nullptr);

  sources_.remove(stage);
  sinks_.remove(stage);
  stages_.remove(stage);

  delete stage;
}

NodeRef Graph::Connect(const OutputRef& output, const InputRef& input) {
  FTL_DCHECK(output.valid());
  FTL_DCHECK(input.valid());

  if (output.connected()) {
    DisconnectOutput(output);
  }
  if (input.connected()) {
    DisconnectInput(input);
  }

  output.actual().Connect(input);
  input.actual().Connect(output);

  return input.node();
}

NodeRef Graph::ConnectNodes(NodeRef upstream_node, NodeRef downstream_node) {
  FTL_DCHECK(upstream_node.valid());
  FTL_DCHECK(downstream_node.valid());
  Connect(upstream_node.output(), downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectOutputToNode(const OutputRef& output,
                                   NodeRef downstream_node) {
  FTL_DCHECK(output.valid());
  FTL_DCHECK(downstream_node.valid());
  Connect(output, downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectNodeToInput(NodeRef upstream_node,
                                  const InputRef& input) {
  FTL_DCHECK(upstream_node.valid());
  FTL_DCHECK(input.valid());
  Connect(upstream_node.output(), input);
  return input.node();
}

void Graph::DisconnectOutput(const OutputRef& output) {
  FTL_DCHECK(output.valid());

  if (!output.connected()) {
    return;
  }

  Input& mate = output.mate().actual();

  if (mate.prepared()) {
    FTL_CHECK(false) << "attempt to disconnect prepared output";
    return;
  }

  mate.Disconnect();
  output.actual().Disconnect();
}

void Graph::DisconnectInput(const InputRef& input) {
  FTL_DCHECK(input.valid());

  if (!input.connected()) {
    return;
  }

  Output& mate = input.mate().actual();

  if (input.actual().prepared()) {
    FTL_CHECK(false) << "attempt to disconnect prepared input";
    return;
  }

  mate.Disconnect();
  input.actual().Disconnect();
}

void Graph::RemoveNodesConnectedToNode(NodeRef node) {
  FTL_DCHECK(node.valid());

  std::deque<NodeRef> to_remove{node};

  while (!to_remove.empty()) {
    NodeRef node = to_remove.front();
    to_remove.pop_front();

    for (size_t i = 0; i < node.input_count(); ++i) {
      to_remove.push_back(node.input(i).node());
    }

    for (size_t i = 0; i < node.output_count(); ++i) {
      to_remove.push_back(node.output(i).node());
    }

    RemoveNode(node);
  }
}

void Graph::RemoveNodesConnectedToOutput(const OutputRef& output) {
  FTL_DCHECK(output.valid());

  if (!output.connected()) {
    return;
  }

  NodeRef downstream_node = output.mate().node();
  DisconnectOutput(output);
  RemoveNodesConnectedToNode(downstream_node);
}

void Graph::RemoveNodesConnectedToInput(const InputRef& input) {
  FTL_DCHECK(input.valid());

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
  while (!stages_.empty()) {
    Stage* stage = stages_.front();
    stages_.pop_front();
    delete stage;
  }
}

void Graph::Prepare() {
  for (Stage* sink : sinks_) {
    for (size_t i = 0; i < sink->input_count(); ++i) {
      engine_.PrepareInput(InputRef(sink, i));
    }
  }
}

void Graph::PrepareInput(const InputRef& input) {
  FTL_DCHECK(input.valid());
  engine_.PrepareInput(input);
}

void Graph::FlushOutput(const OutputRef& output) {
  FTL_DCHECK(output);
  engine_.FlushOutput(output);
}

void Graph::FlushAllOutputs(NodeRef node) {
  FTL_DCHECK(node.valid());
  size_t output_count = node.output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    FlushOutput(node.output(output_index));
  }
}

NodeRef Graph::Add(Stage* stage) {
  stages_.push_back(stage);

  if (stage->input_count() == 0) {
    sources_.push_back(stage);
  }

  if (stage->output_count() == 0) {
    sinks_.push_back(stage);
  }

  stage->SetUpdateCallback(update_function_);

  return NodeRef(stage);
}

}  // namespace media
