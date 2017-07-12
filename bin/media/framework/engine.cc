// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/engine.h"

namespace media {

Engine::Engine() {}

Engine::~Engine() {}

void Engine::PrepareInput(Input* input) {
  FTL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const Stage::UpstreamCallback& callback) {
    FTL_DCHECK(input);
    FTL_DCHECK(output);
    FTL_DCHECK(!input->prepared());
    PayloadAllocator* allocator = input->stage()->PrepareInput(input->index());
    input->set_prepared(true);
    output->stage()->PrepareOutput(output->index(), allocator, callback);
  });
}

void Engine::UnprepareInput(Input* input) {
  FTL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const Stage::UpstreamCallback& callback) {
    FTL_DCHECK(input);
    FTL_DCHECK(output);
    FTL_DCHECK(input->prepared());
    input->stage()->UnprepareInput(input->index());
    output->stage()->UnprepareOutput(output->index(), callback);
  });
}

void Engine::FlushOutput(Output* output) {
  FTL_DCHECK(output);
  if (!output->connected()) {
    return;
  }

  VisitDownstream(output, [](Output* output, Input* input,
                             const Stage::DownstreamCallback& callback) {
    FTL_DCHECK(output);
    FTL_DCHECK(input);
    FTL_DCHECK(input->prepared());
    output->stage()->FlushOutput(output->index());
    input->stage()->FlushInput(input->index(), callback);
  });
}

void Engine::RequestUpdate(Stage* stage) {
  FTL_DCHECK(stage);
  ftl::MutexLocker locker(&mutex_);
  Update(stage);
  Update();
}

void Engine::PushToSupplyBacklog(Stage* stage) {
  mutex_.AssertHeld();
  FTL_DCHECK(stage);

  packets_produced_ = true;
  if (!stage->in_supply_backlog_) {
    supply_backlog_.push(stage);
    stage->in_supply_backlog_ = true;
  }
}

void Engine::PushToDemandBacklog(Stage* stage) {
  mutex_.AssertHeld();
  FTL_DCHECK(stage);

  if (!stage->in_demand_backlog_) {
    demand_backlog_.push(stage);
    stage->in_demand_backlog_ = true;
  }
}

void Engine::VisitUpstream(Input* input, const UpstreamVisitor& visitor) {
  FTL_DCHECK(input);
  ftl::MutexLocker locker(&mutex_);

  std::queue<Input*> backlog;
  backlog.push(input);

  while (!backlog.empty()) {
    Input* input = backlog.front();
    backlog.pop();
    FTL_DCHECK(input);
    FTL_DCHECK(input->connected());

    Output* output = input->mate();
    Stage* output_stage = output->stage();

    visitor(input, output, [output_stage, &backlog](size_t input_index) {
      backlog.push(&output_stage->input(input_index));
    });
  }
}

void Engine::VisitDownstream(Output* output, const DownstreamVisitor& visitor) {
  FTL_DCHECK(output);
  ftl::MutexLocker locker(&mutex_);

  std::queue<Output*> backlog;
  backlog.push(output);

  while (!backlog.empty()) {
    Output* output = backlog.front();
    backlog.pop();
    FTL_DCHECK(output);
    FTL_DCHECK(output->connected());

    Input* input = output->mate();
    Stage* input_stage = input->stage();

    visitor(output, input, [input_stage, &backlog](size_t output_index) {
      backlog.push(&input_stage->output(output_index));
    });
  }
}

void Engine::Update() {
  mutex_.AssertHeld();

  while (true) {
    Stage* stage = PopFromSupplyBacklog();
    if (stage != nullptr) {
      Update(stage);
      continue;
    }

    stage = PopFromDemandBacklog();
    if (stage != nullptr) {
      Update(stage);
      continue;
    }

    break;
  }
}

void Engine::Update(Stage* stage) {
  mutex_.AssertHeld();

  FTL_DCHECK(stage);

  packets_produced_ = false;

  stage->Update(this);

  // If the stage produced packets, it may need to reevaluate demand later.
  if (packets_produced_) {
    PushToDemandBacklog(stage);
  }
}

Stage* Engine::PopFromSupplyBacklog() {
  mutex_.AssertHeld();

  if (supply_backlog_.empty()) {
    return nullptr;
  }

  Stage* stage = supply_backlog_.front();
  supply_backlog_.pop();
  FTL_DCHECK(stage->in_supply_backlog_);
  stage->in_supply_backlog_ = false;
  return stage;
}

Stage* Engine::PopFromDemandBacklog() {
  mutex_.AssertHeld();

  if (demand_backlog_.empty()) {
    return nullptr;
  }

  Stage* stage = demand_backlog_.top();
  demand_backlog_.pop();
  FTL_DCHECK(stage->in_demand_backlog_);
  stage->in_demand_backlog_ = false;
  return stage;
}

}  // namespace media
