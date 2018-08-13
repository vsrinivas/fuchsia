// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/graph.h"

#include "garnet/bin/mediaplayer/framework/formatting.h"
#include "garnet/bin/mediaplayer/util/callback_joiner.h"
#include "garnet/bin/mediaplayer/util/threadsafe_callback_joiner.h"

namespace media_player {

Graph::Graph(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

Graph::~Graph() { Reset(); }

void Graph::RemoveNode(NodeRef node) {
  FXL_DCHECK(node);

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
  stages_.remove(stage->shared_from_this());
}

NodeRef Graph::Connect(const OutputRef& output, const InputRef& input) {
  FXL_DCHECK(output);
  FXL_DCHECK(input);

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

  if (mate->prepared()) {
    FXL_CHECK(false) << "attempt to disconnect prepared input " << *mate;
    return;
  }

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

  if (actual_input->prepared()) {
    FXL_CHECK(false) << "attempt to disconnect prepared input "
                     << *actual_input;
    return;
  }

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

  for (auto& stage : stages_) {
    stage->Acquire(joiner->NewCallback());
  }

  joiner->WhenJoined(dispatcher_, [stages = std::move(stages_)]() mutable {
    while (!stages.empty()) {
      std::shared_ptr<StageImpl> stage = stages.front();
      stages.pop_front();
      stage->ShutDown();
    }
  });
}

void Graph::Prepare() {
  for (StageImpl* sink : sinks_) {
    for (size_t i = 0; i < sink->input_count(); ++i) {
      PrepareInput(&sink->input(i));
    }
  }
}

void Graph::PrepareInput(const InputRef& input) {
  FXL_DCHECK(input);
  PrepareInput(input.actual());
}

void Graph::Unprepare() {
  for (StageImpl* sink : sinks_) {
    for (size_t i = 0; i < sink->input_count(); ++i) {
      UnprepareInput(&sink->input(i));
    }
  }
}

void Graph::UnprepareInput(const InputRef& input) {
  FXL_DCHECK(input);
  UnprepareInput(input.actual());
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

void Graph::PostTask(fit::closure task, std::initializer_list<NodeRef> nodes) {
  auto joiner = ThreadsafeCallbackJoiner::Create();

  std::vector<StageImpl*> stages;
  for (NodeRef node : nodes) {
    node.stage_->Acquire(joiner->NewCallback());
    stages.push_back(node.stage_);
  }

  joiner->WhenJoined(dispatcher_,
                     [task = std::move(task), stages = std::move(stages)]() {
                       task();
                       for (auto stage : stages) {
                         stage->Release();
                       }
                     });
}

NodeRef Graph::AddStage(std::shared_ptr<StageImpl> stage) {
  FXL_DCHECK(stage);
  FXL_DCHECK(dispatcher_);

  stage->SetDispatcher(dispatcher_);
  stages_.push_back(stage);

  if (stage->input_count() == 0) {
    sources_.push_back(stage.get());
  }

  if (stage->output_count() == 0) {
    sinks_.push_back(stage.get());
  }

  return NodeRef(stage.get());
}

void Graph::FlushOutputs(std::queue<Output*>* backlog, bool hold_frame,
                         fit::closure callback) {
  FXL_DCHECK(backlog);

  auto callback_joiner = CallbackJoiner::Create();

  // Walk the graph downstream from the outputs already in the backlog until
  // we hit a sink. The |FlushOutput| and |FlushInput| calls are all issued
  // synchronously from this loop, and then we wait for all the callbacks to
  // be called. This works, because downstream flow is halted synchronously,
  // even though the nodes may have additional flushing business that needs
  // time to complete.
  while (!backlog->empty()) {
    Output* output = backlog->front();
    backlog->pop();
    FXL_DCHECK(output);

    if (!output->connected()) {
      continue;
    }

    Input* input = output->mate();
    FXL_DCHECK(input);
    FXL_DCHECK(input->prepared())
        << "Attempt to flush unprepared input " << *input;
    StageImpl* input_stage = input->stage();

    output->stage()->FlushOutput(output->index(),
                                 callback_joiner->NewCallback());

    input_stage->FlushInput(input->index(), hold_frame,
                            callback_joiner->NewCallback());

    for (size_t output_index = 0; output_index < input_stage->output_count();
         ++output_index) {
      backlog->push(&input_stage->output(output_index));
    }
  }

  callback_joiner->WhenJoined(std::move(callback));
}

void Graph::PrepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(!input->prepared()) << *input << " already prepared.";
    std::shared_ptr<PayloadAllocator> allocator =
        input->stage()->PrepareInput(input->index());
    input->set_prepared(true);
    output->stage()->PrepareOutput(output->index(), allocator);
  });
}

void Graph::UnprepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(input->prepared()) << *input << " already unprepared.";
    input->stage()->UnprepareInput(input->index());
    input->set_prepared(false);
    output->stage()->UnprepareOutput(output->index());
  });
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
    StageImpl* output_stage = output->stage();

    visitor(input, output);

    for (size_t input_index = 0; input_index < output_stage->input_count();
         ++input_index) {
      backlog.push(&output_stage->input(input_index));
    }
  }
}

}  // namespace media_player
