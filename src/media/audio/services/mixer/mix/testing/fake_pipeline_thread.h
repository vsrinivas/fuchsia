// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/pipeline_thread.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// A very simple no-op implementation of PipelineThread.
class FakePipelineThread : public PipelineThread {
 public:
  explicit FakePipelineThread(ThreadId id) : id_(id), name_("FakeThread" + std::to_string(id)) {}

  // Implementation of Thread.
  ThreadId id() const { return id_; }
  std::string_view name() const { return name_; }
  const ThreadChecker& checker() const { return checker_; }

 private:
  const ThreadId id_;
  const std::string name_;
  const ThreadChecker checker_{std::nullopt};  // FakeThreads don't need to be checked
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_
