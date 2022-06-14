// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PTR_DECLS_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PTR_DECLS_H_

#include <memory>

namespace media_audio {

// This file exists to break circular dependencies.
// Since shared_ptr use is ubiquitous, we use XPtr as a more concise name for std::shared_ptr<X>.

class Thread;
using ThreadPtr = std::shared_ptr<Thread>;

class DetachedThread;
using DetachedThreadPtr = std::shared_ptr<DetachedThread>;

class MixThread;
using MixThreadPtr = std::shared_ptr<MixThread>;

class PipelineStage;
using PipelineStagePtr = std::shared_ptr<PipelineStage>;

class ConsumerStage;
using ConsumerStagePtr = std::shared_ptr<ConsumerStage>;

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PTR_DECLS_H_
