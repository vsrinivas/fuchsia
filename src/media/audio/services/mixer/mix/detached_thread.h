// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_DETACHED_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_DETACHED_THREAD_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/profile.h>

#include <memory>
#include <string>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/thread.h"

namespace media_audio {

// A detached thread controls PipelineStages that are not connected to any ConsumerStage, i.e. it
// controls "detached" stages. There is exactly one DetachedThread for every graph. This is not
// backed by a real kernel thread. Tasks assigned to this may be executed on any kernel thread,
// hence we use kAnyThreadId for the DetachedThread's ID. It is illegal to assign a ConsumerStage to
// the DetachedThread -- calling `AddConsumer` will crash.
//
// See discussion in ../docs/execution_model.md.
class DetachedThread : public Thread {
 public:
  static DetachedThreadPtr Create();

  // The value returned by `id()`.
  // Since there is exactly one DetachedThread per graph, this is a unique identifier.
  static inline constexpr ThreadId kId = kAnyThreadId;

  // Implementation of Thread.
  ThreadId id() const final { return kId; }
  std::string_view name() const final { return name_; }
  const ThreadChecker& checker() const final { return checker_; }
  void AddConsumer(ConsumerStagePtr consumer) final {
    UNREACHABLE << "Consumers should never be assigned to the DetachedThread";
  }
  void RemoveConsumer(ConsumerStagePtr consumer) final {
    UNREACHABLE << "Consumers should never be assigned to the DetachedThread";
  }

 private:
  DetachedThread() = default;

  const std::string name_{"DetachedThread"};

  // If an object is controlled by the detached thread, it can be mutated from any
  // thread as long as the mutations are appropriately serialized. See ../README.md.
  const ThreadChecker checker_{std::nullopt};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_DETACHED_THREAD_H_
