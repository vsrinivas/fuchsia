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

void Graph::RemovePart(PartRef part) {
  FTL_DCHECK(part.valid());

  Stage* stage = part.stage_;

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

PartRef Graph::Connect(const OutputRef& output, const InputRef& input) {
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

  return input.part();
}

PartRef Graph::ConnectParts(PartRef upstream_part, PartRef downstream_part) {
  FTL_DCHECK(upstream_part.valid());
  FTL_DCHECK(downstream_part.valid());
  Connect(upstream_part.output(), downstream_part.input());
  return downstream_part;
}

PartRef Graph::ConnectOutputToPart(const OutputRef& output,
                                   PartRef downstream_part) {
  FTL_DCHECK(output.valid());
  FTL_DCHECK(downstream_part.valid());
  Connect(output, downstream_part.input());
  return downstream_part;
}

PartRef Graph::ConnectPartToInput(PartRef upstream_part,
                                  const InputRef& input) {
  FTL_DCHECK(upstream_part.valid());
  FTL_DCHECK(input.valid());
  Connect(upstream_part.output(), input);
  return input.part();
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

void Graph::RemovePartsConnectedToPart(PartRef part) {
  FTL_DCHECK(part.valid());

  std::deque<PartRef> to_remove{part};

  while (!to_remove.empty()) {
    PartRef part = to_remove.front();
    to_remove.pop_front();

    for (size_t i = 0; i < part.input_count(); ++i) {
      to_remove.push_back(part.input(i).part());
    }

    for (size_t i = 0; i < part.output_count(); ++i) {
      to_remove.push_back(part.output(i).part());
    }

    RemovePart(part);
  }
}

void Graph::RemovePartsConnectedToOutput(const OutputRef& output) {
  FTL_DCHECK(output.valid());

  if (!output.connected()) {
    return;
  }

  PartRef downstream_part = output.mate().part();
  DisconnectOutput(output);
  RemovePartsConnectedToPart(downstream_part);
}

void Graph::RemovePartsConnectedToInput(const InputRef& input) {
  FTL_DCHECK(input.valid());

  if (!input.connected()) {
    return;
  }

  PartRef upstream_part = input.mate().part();
  DisconnectInput(input);
  RemovePartsConnectedToPart(upstream_part);
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

void Graph::FlushAllOutputs(PartRef part) {
  FTL_DCHECK(part.valid());
  size_t output_count = part.output_count();
  for (size_t output_index = 0; output_index < output_count; output_index++) {
    FlushOutput(part.output(output_index));
  }
}

PartRef Graph::Add(Stage* stage) {
  stages_.push_back(stage);

  if (stage->input_count() == 0) {
    sources_.push_back(stage);
  }

  if (stage->output_count() == 0) {
    sinks_.push_back(stage);
  }

  stage->SetUpdateCallback(update_function_);

  return PartRef(stage);
}

}  // namespace media
