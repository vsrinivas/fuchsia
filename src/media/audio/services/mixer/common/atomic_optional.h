// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_ATOMIC_OPTIONAL_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_ATOMIC_OPTIONAL_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <mutex>
#include <optional>

namespace media_audio {

// An atomic container for an optional value. The value can be atomically swapped or retrieved. This
// is similar in spirit to `std::atomic<std::optional<T>>`, but can store any movable type `T` and
// has a more restricted set of operations.
template <typename T>
class AtomicOptional {
 public:
  AtomicOptional() = default;
  AtomicOptional(const AtomicOptional&) = delete;
  AtomicOptional& operator=(const AtomicOptional&) = delete;
  AtomicOptional(AtomicOptional&&) = delete;
  AtomicOptional& operator=(AtomicOptional&&) = delete;

  // Swaps the value, returning the old value if any, or returns std::nullopt if there was no value.
  std::optional<T> swap(T new_value) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto old = std::move(value_);
    value_ = std::move(new_value);
    return old;
  }

  // Pops the value if any, or returns std::nullopt if there is no value. After this returns, the
  // value is removed and a subsequent pop will return std::nullopt.
  std::optional<T> pop() {
    std::lock_guard<std::mutex> guard(mutex_);
    auto old = std::move(value_);
    value_ = std::nullopt;
    return old;
  }

  // Unconditionally stores `value`, crashing if a value is already stored.
  // Primarily useful in tests.
  void set_must_be_empty(T value) {
    std::lock_guard<std::mutex> guard(mutex_);
    FX_CHECK(!value_);
    value_ = std::move(value);
  }

 private:
  std::mutex mutex_;
  std::optional<T> value_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_ATOMIC_OPTIONAL_H_
