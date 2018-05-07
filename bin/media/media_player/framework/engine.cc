// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/engine.h"

namespace media_player {

Engine::Engine() {}

Engine::~Engine() {}

void Engine::PrepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          StageImpl::UpstreamCallback callback) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(!input->prepared());
    std::shared_ptr<PayloadAllocator> allocator =
        input->stage()->PrepareInput(input->index());
    input->set_prepared(true);
    output->stage()->PrepareOutput(output->index(), allocator, callback);
  });
}

void Engine::UnprepareInput(Input* input) {
  FXL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          StageImpl::UpstreamCallback callback) {
    FXL_DCHECK(input);
    FXL_DCHECK(output);
    FXL_DCHECK(input->prepared());
    input->stage()->UnprepareInput(input->index());
    output->stage()->UnprepareOutput(output->index(), callback);
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

}  // namespace media_player
