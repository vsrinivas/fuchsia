// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_VERSIONED_TIMELINE_FUNCTION_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_VERSIONED_TIMELINE_FUNCTION_H_

#include <lib/media/cpp/timeline_function.h>

#include <mutex>

#include <fbl/ref_counted.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

class VersionedTimelineFunction : public fbl::RefCounted<VersionedTimelineFunction> {
 public:
  VersionedTimelineFunction() = default;

  explicit VersionedTimelineFunction(TimelineFunction initial_function)
      : function_(initial_function) {}

  virtual ~VersionedTimelineFunction() = default;

  void Update(TimelineFunction fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fn != function_) {
      function_ = fn;
      generation_.Next();
    }
  }

  std::pair<TimelineFunction, uint32_t> get() const { return Snapshot(); }

  int64_t Apply(int64_t reference_input) const { return get().first.Apply(reference_input); }

 protected:
  friend class DerivedTimelineFunction;

  virtual std::pair<TimelineFunction, uint32_t> Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::make_pair(function_, generation_.get());
  }

 private:
  mutable std::mutex mutex_;
  TimelineFunction function_ FXL_GUARDED_BY(mutex_);
  GenerationId generation_ FXL_GUARDED_BY(mutex_);
};

// A DerivedTimelineFunction is a function that is always composed with another
// VersionedTimelineFunction. Calls to DerivedTimelineFunction::Update will only update the portion
// of the timeline function that is composed with the underlying VersionedTimelineFunction.
class DerivedTimelineFunction : public VersionedTimelineFunction {
 public:
  explicit DerivedTimelineFunction(fbl::RefPtr<VersionedTimelineFunction> base)
      : VersionedTimelineFunction(), base_(std::move(base)) {}

  DerivedTimelineFunction(fbl::RefPtr<VersionedTimelineFunction> base,
                          TimelineFunction initial_transform)
      : VersionedTimelineFunction(initial_transform), base_(std::move(base)) {}

 protected:
  std::pair<TimelineFunction, uint32_t> Snapshot() const override {
    auto transform = VersionedTimelineFunction::Snapshot();
    auto base = base_->Snapshot();
    return {TimelineFunction::Compose(transform.first, base.first), transform.second + base.second};
  }

 private:
  fbl::RefPtr<VersionedTimelineFunction> base_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_VERSIONED_TIMELINE_FUNCTION_H_
