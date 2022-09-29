// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_PIPELINE_MIX_THREAD_WITHOUT_LOOP_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_PIPELINE_MIX_THREAD_WITHOUT_LOOP_H_

#include "src/media/audio/services/mixer/mix/pipeline_mix_thread.h"

namespace media_audio {

// Creates a PipelineMixThread that does not include a kernel thread. Useful in tests that want to
// perform mix jobs explicitly.
std::shared_ptr<PipelineMixThread> CreatePipelineMixThreadWithoutLoop(PipelineMixThread::Args args);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_PIPELINE_MIX_THREAD_WITHOUT_LOOP_H_
