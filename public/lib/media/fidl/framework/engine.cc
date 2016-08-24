// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/engine.h"

namespace mojo {
namespace media {

Engine::Engine() {}

Engine::~Engine() {
  base::AutoLock lock(lock_);
}

void Engine::PrepareInput(const InputRef& input) {
  VisitUpstream(input, [](const InputRef& input, const OutputRef& output,
                          const Stage::UpstreamCallback& callback) {
    DCHECK(!input.actual().prepared());
    PayloadAllocator* allocator = input.stage_->PrepareInput(input.index_);
    input.actual().set_prepared(true);
    output.stage_->PrepareOutput(output.index_, allocator, callback);
  });
}

void Engine::UnprepareInput(const InputRef& input) {
  VisitUpstream(input, [](const InputRef& input, const OutputRef& output,
                          const Stage::UpstreamCallback& callback) {
    DCHECK(input.actual().prepared());
    input.stage_->UnprepareInput(input.index_);
    output.stage_->UnprepareOutput(output.index_, callback);
  });
}

void Engine::FlushOutput(const OutputRef& output) {
  if (!output.connected()) {
    return;
  }
  VisitDownstream(output, [](const OutputRef& output, const InputRef& input,
                             const Stage::DownstreamCallback& callback) {
    DCHECK(input.actual().prepared());
    output.stage_->FlushOutput(output.index_);
    input.stage_->FlushInput(input.index_, callback);
  });
}

void Engine::RequestUpdate(Stage* stage) {
  DCHECK(stage);
  base::AutoLock lock(lock_);
  Update(stage);
  Update();
}

void Engine::PushToSupplyBacklog(Stage* stage) {
  lock_.AssertAcquired();
  DCHECK(stage);

  packets_produced_ = true;
  if (!stage->in_supply_backlog_) {
    supply_backlog_.push(stage);
    stage->in_supply_backlog_ = true;
  }
}

void Engine::PushToDemandBacklog(Stage* stage) {
  lock_.AssertAcquired();
  DCHECK(stage);

  if (!stage->in_demand_backlog_) {
    demand_backlog_.push(stage);
    stage->in_demand_backlog_ = true;
  }
}

void Engine::VisitUpstream(const InputRef& input,
                           const UpstreamVisitor& vistor) {
  base::AutoLock lock(lock_);

  std::queue<InputRef> backlog;
  backlog.push(input);

  while (!backlog.empty()) {
    InputRef input = backlog.front();
    backlog.pop();
    DCHECK(input.valid());
    DCHECK(input.connected());

    const OutputRef& output = input.mate();
    Stage* output_stage = output.stage_;

    vistor(input, output, [output_stage, &backlog](size_t input_index) {
      backlog.push(InputRef(output_stage, input_index));
    });
  }
}

void Engine::VisitDownstream(const OutputRef& output,
                             const DownstreamVisitor& vistor) {
  base::AutoLock lock(lock_);

  std::queue<OutputRef> backlog;
  backlog.push(output);

  while (!backlog.empty()) {
    OutputRef output = backlog.front();
    backlog.pop();
    DCHECK(output.valid());
    DCHECK(output.connected());

    const InputRef& input = output.mate();
    Stage* input_stage = input.stage_;

    vistor(output, input, [input_stage, &backlog](size_t output_index) {
      backlog.push(OutputRef(input_stage, output_index));
    });
  }
}

void Engine::Update() {
  lock_.AssertAcquired();

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
  lock_.AssertAcquired();

  DCHECK(stage);

  packets_produced_ = false;

  stage->Update(this);

  // If the stage produced packets, it may need to reevaluate demand later.
  if (packets_produced_) {
    PushToDemandBacklog(stage);
  }
}

Stage* Engine::PopFromSupplyBacklog() {
  lock_.AssertAcquired();

  if (supply_backlog_.empty()) {
    return nullptr;
  }

  Stage* stage = supply_backlog_.front();
  supply_backlog_.pop();
  DCHECK(stage->in_supply_backlog_);
  stage->in_supply_backlog_ = false;
  return stage;
}

Stage* Engine::PopFromDemandBacklog() {
  lock_.AssertAcquired();

  if (demand_backlog_.empty()) {
    return nullptr;
  }

  Stage* stage = demand_backlog_.top();
  demand_backlog_.pop();
  DCHECK(stage->in_demand_backlog_);
  stage->in_demand_backlog_ = false;
  return stage;
}

}  // namespace media
}  // namespace mojo
