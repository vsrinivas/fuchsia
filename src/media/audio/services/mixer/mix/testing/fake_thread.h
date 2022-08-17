// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>

#include "src/media/audio/services/mixer/mix/thread.h"

namespace media_audio {

class FakeThread;
using FakeThreadPtr = std::shared_ptr<FakeThread>;

// A very simple no-op implementation of Thread.
class FakeThread : public Thread {
 public:
  static FakeThreadPtr Create(ThreadId id) {
    FX_CHECK(id != kAnyThreadId);
    return FakeThreadPtr(new FakeThread(id));
  }

  const std::unordered_set<ConsumerStagePtr>& consumers() const { return consumers_; }

  // Implementation of Thread.
  ThreadId id() const { return id_; }
  std::string_view name() const { return name_; }
  const ThreadChecker& checker() const { return checker_; }
  void AddConsumer(ConsumerStagePtr consumer) { consumers_.insert(consumer); }
  void RemoveConsumer(ConsumerStagePtr consumer) { consumers_.erase(consumer); }

 private:
  explicit FakeThread(ThreadId id) : id_(id), name_("FakeThread" + std::to_string(id)) {}

  const ThreadId id_;
  const std::string name_;
  const ThreadChecker checker_{std::nullopt};  // FakeThreads don't need to be checked

  std::unordered_set<ConsumerStagePtr> consumers_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_FAKE_THREAD_H_
