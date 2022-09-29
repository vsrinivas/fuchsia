// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/testing/pipeline_mix_thread_without_loop.h"

namespace media_audio {

std::shared_ptr<PipelineMixThread> CreatePipelineMixThreadWithoutLoop(
    PipelineMixThread::Args args) {
  return PipelineMixThread::CreateWithoutLoop(std::move(args));
}

}  // namespace media_audio
