// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/testing/graph_mix_thread_without_loop.h"

namespace media_audio {

std::shared_ptr<GraphMixThread> CreateGraphMixThreadWithoutLoop(PipelineMixThread::Args args) {
  auto q = args.global_task_queue;
  return std::shared_ptr<GraphMixThread>(
      new GraphMixThread(q, CreatePipelineMixThreadWithoutLoop(std::move(args))));
}

}  // namespace media_audio
