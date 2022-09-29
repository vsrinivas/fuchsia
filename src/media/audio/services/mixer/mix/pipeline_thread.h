// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_THREAD_H_

#include <memory>
#include <string>

#include "src/media/audio/services/common/thread_checker.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// An abstract base class for threads.
//
// As a general rule, const methods are safe to call from any thread, while mutable methods are not
// thread safe, but see individual methods for specific semantics.
class PipelineThread {
 public:
  // Returns the thread's ID.
  // This is guaranteed to be a unique identifier.
  // Safe to call from any thread.
  virtual ThreadId id() const = 0;

  // Returns the thread's name. This is used for diagnostics only.
  // The name may not be a unique identifier.
  // Safe to call from any thread.
  virtual std::string_view name() const = 0;

  // Returns a checker which validates that code is running on this thread.
  // Safe to call from any thread.
  virtual const ThreadChecker& checker() const = 0;

 protected:
  PipelineThread() = default;
  virtual ~PipelineThread() = default;

  PipelineThread(const PipelineThread&) = delete;
  PipelineThread& operator=(const PipelineThread&) = delete;

  PipelineThread(PipelineThread&&) = delete;
  PipelineThread& operator=(PipelineThread&&) = delete;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PIPELINE_THREAD_H_
