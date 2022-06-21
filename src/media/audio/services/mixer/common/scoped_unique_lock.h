// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_SCOPED_UNIQUE_LOCK_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_SCOPED_UNIQUE_LOCK_H_

#include <lib/zircon-internal/thread_annotations.h>

#include <mutex>

namespace media_audio {

// A wrapper around std::unique_lock<Mutex> that enables thread-safety analysis.
// The lock may be passed to std::condition_variable::wait, but explicit lock/unlock
// methods have been removed.
//
// Thread-safety analysis does not support the standard implementation of std::unique_lock
// because that class supports optional locking (e.g. the lock may or may not be held in the
// constructor). In practice we don't need those features.
template <typename Mutex>
class TA_SCOPED_CAP scoped_unique_lock : public std::unique_lock<Mutex> {
 public:
  explicit scoped_unique_lock(Mutex& m) TA_ACQ(m) : std::unique_lock<Mutex>(m), m_(m) {}
  ~scoped_unique_lock() TA_REL() = default;

 private:
  Mutex& m_;

  // Disallow calling these to preserve the "scoped" property.
  using std::unique_lock<Mutex>::lock;
  using std::unique_lock<Mutex>::release;
  using std::unique_lock<Mutex>::try_lock;
  using std::unique_lock<Mutex>::try_lock_for;
  using std::unique_lock<Mutex>::try_lock_until;
  using std::unique_lock<Mutex>::unlock;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_SCOPED_UNIQUE_LOCK_H_
