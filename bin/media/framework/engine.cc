// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/engine.h"

namespace media {

Engine::Engine() {}

Engine::~Engine() {}

void Engine::PrepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const StageImpl::UpstreamCallback& callback) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(!input->prepared());
    PayloadAllocator* allocator = input->stage()->PrepareInput(input->index());
    input->set_prepared(true);
    output->stage()->PrepareOutput(output->index(), allocator, callback);
  });
}

void Engine::UnprepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const StageImpl::UpstreamCallback& callback) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(input->prepared());
    input->stage()->UnprepareInput(input->index());
    output->stage()->UnprepareOutput(output->index(), callback);
  });
}

void Engine::FlushOutput(Output* output, bool hold_frame) {
  FXL_DCHECK(output);
  if (!output->connected()) {
    return;
  }

  VisitDownstream(
      output, [hold_frame](Output* output, Input* input,
                           const StageImpl::DownstreamCallback& callback) {
        FXL_DCHECK(output);
        FXL_DCHECK(input);
        FXL_DCHECK(input->prepared());
        output->stage()->FlushOutput(output->index());
        input->stage()->FlushInput(input->index(), hold_frame, callback);
      });
}

void Engine::VisitUpstream(Input* input, const UpstreamVisitor& visitor) {
  FXL_DCHECK(input);

  std::queue<Input*> backlog;
  backlog.push(input);

  while (!backlog.empty()) {
    Input* input = backlog.front();
    backlog.pop();
    FXL_DCHECK(input);
    FXL_DCHECK(input->connected());

    Output* output = input->mate();
    StageImpl* output_stage = output->stage();

    visitor(input, output, [output_stage, &backlog](size_t input_index) {
      backlog.push(&output_stage->input(input_index));
    });
  }
}

void Engine::VisitDownstream(Output* output, const DownstreamVisitor& visitor) {
  FXL_DCHECK(output);

  std::queue<Output*> backlog;
  backlog.push(output);

  while (!backlog.empty()) {
    Output* output = backlog.front();
    backlog.pop();
    FXL_DCHECK(output);
    FXL_DCHECK(output->connected());

    Input* input = output->mate();
    StageImpl* input_stage = input->stage();

    visitor(output, input, [input_stage, &backlog](size_t output_index) {
      backlog.push(&input_stage->output(output_index));
    });
  }
}

}  // namespace media
