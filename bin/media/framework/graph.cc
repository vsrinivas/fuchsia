// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/graph.h"

namespace media {

Graph::Graph(ftl::RefPtr<ftl::TaskRunner> default_task_runner)
    : default_task_runner_(default_task_runner) {}

Graph::~Graph() {
  Reset();
}

void Graph::RemoveNode(NodeRef node) {
  FTL_DCHECK(node);

  StageImpl* stage = node.stage_;

  size_t input_count = stage->input_count();
  for (size_t input_index = 0; input_index < input_count; input_index++) {
    Input& input = stage->input(input_index);
    if (input.connected()) {
      DisconnectInput(InputRef(&input));
    }
  }

  size_t output_count = stage->output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    Output& output = stage->output(output_index);
    if (output.connected()) {
      DisconnectOutput(OutputRef(&output));
    }
  }

  sources_.remove(stage);
  sinks_.remove(stage);
  stages_.remove(stage);

  delete stage;
}

NodeRef Graph::Connect(const OutputRef& output, const InputRef& input) {
  FTL_DCHECK(output);
  FTL_DCHECK(input);

  if (output.connected()) {
    DisconnectOutput(output);
  }
  if (input.connected()) {
    DisconnectInput(input);
  }

  output.actual()->Connect(input.actual());
  input.actual()->Connect(output.actual());

  return input.node();
}

NodeRef Graph::ConnectNodes(NodeRef upstream_node, NodeRef downstream_node) {
  FTL_DCHECK(upstream_node);
  FTL_DCHECK(downstream_node);
  Connect(upstream_node.output(), downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectOutputToNode(const OutputRef& output,
                                   NodeRef downstream_node) {
  FTL_DCHECK(output);
  FTL_DCHECK(downstream_node);
  Connect(output, downstream_node.input());
  return downstream_node;
}

NodeRef Graph::ConnectNodeToInput(NodeRef upstream_node,
                                  const InputRef& input) {
  FTL_DCHECK(upstream_node);
  FTL_DCHECK(input);
  Connect(upstream_node.output(), input);
  return input.node();
}

void Graph::DisconnectOutput(const OutputRef& output) {
  FTL_DCHECK(output);

  if (!output.connected()) {
    return;
  }

  Output* actual_output = output.actual();
  FTL_DCHECK(actual_output);
  Input* mate = actual_output->mate();
  FTL_DCHECK(mate);

  if (mate->prepared()) {
    FTL_CHECK(false) << "attempt to disconnect prepared output";
    return;
  }

  mate->Disconnect();
  actual_output->Disconnect();
}

void Graph::DisconnectInput(const InputRef& input) {
  FTL_DCHECK(input);

  if (!input.connected()) {
    return;
  }

  Input* actual_input = input.actual();
  FTL_DCHECK(actual_input);
  Output* mate = actual_input->mate();
  FTL_DCHECK(mate);

  if (actual_input->prepared()) {
    FTL_CHECK(false) << "attempt to disconnect prepared input";
    return;
  }

  mate->Disconnect();
  actual_input->Disconnect();
}

void Graph::RemoveNodesConnectedToNode(NodeRef node) {
  FTL_DCHECK(node);

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
  FTL_DCHECK(output);

  if (!output.connected()) {
    return;
  }

  NodeRef downstream_node = output.mate().node();
  DisconnectOutput(output);
  RemoveNodesConnectedToNode(downstream_node);
}

void Graph::RemoveNodesConnectedToInput(const InputRef& input) {
  FTL_DCHECK(input);

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
    StageImpl* stage = stages_.front();
    stages_.pop_front();
    delete stage;
  }
}

void Graph::Prepare() {
  for (StageImpl* sink : sinks_) {
    for (size_t i = 0; i < sink->input_count(); ++i) {
      engine_.PrepareInput(&sink->input(i));
    }
  }
}

void Graph::PrepareInput(const InputRef& input) {
  FTL_DCHECK(input);
  engine_.PrepareInput(input.actual());
}

void Graph::FlushOutput(const OutputRef& output, bool hold_frame) {
  FTL_DCHECK(output);
  engine_.FlushOutput(output.actual(), hold_frame);
}

void Graph::FlushAllOutputs(NodeRef node, bool hold_frame) {
  FTL_DCHECK(node);
  size_t output_count = node.output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    FlushOutput(node.output(output_index), hold_frame);
  }
}

void Graph::PostTask(const ftl::Closure& task,
                     std::initializer_list<NodeRef> nodes) {
  std::vector<StageImpl*> stages;
  for (NodeRef node : nodes) {
    stages.push_back(node.stage_);
  }

  struct PostedTask {
    PostedTask(const ftl::Closure& task, std::vector<StageImpl*> stages)
        : task_(task), stages_(std::move(stages)) {
      unacquired_stage_counter_ = stages_.size();
    }

    ftl::Closure task_;
    std::vector<StageImpl*> stages_;
    std::atomic_uint32_t unacquired_stage_counter_;
  };

  std::shared_ptr<PostedTask> posted_task =
      std::make_shared<PostedTask>(task, std::move(stages));

  for (StageImpl* stage : posted_task->stages_) {
    stage->Acquire([posted_task]() {
      if (--(posted_task->unacquired_stage_counter_) != 0) {
        return;
      }

      posted_task->task_();

      for (StageImpl* stage : posted_task->stages_) {
        stage->Release();
      }
    });
  }
}

NodeRef Graph::Add(StageImpl* stage, ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(stage);
  FTL_DCHECK(task_runner || default_task_runner_);

  stage->SetTaskRunner(task_runner ? task_runner : default_task_runner_);
  stages_.push_back(stage);

  if (stage->input_count() == 0) {
    sources_.push_back(stage);
  }

  if (stage->output_count() == 0) {
    sinks_.push_back(stage);
  }

  return NodeRef(stage);
}

}  // namespace media
