// // Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_THREAD_CHECKER_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_THREAD_CHECKER_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <optional>
#include <thread>

namespace media_audio {

// A class to validate that operations happen on a specific thread.
// This is like fit::thread_checker, with three differences:
//
//   1. The id is optional. If not specified, the calling code is allowed
//      to run on any thread.
//
//   2. Checks are always on, while fit::thread_checker disables checks
//      in release builds.
//
//   3. Scoped capabilities are supported via the ScopedThreadChecker
//      class, rather than std::lock_guard().
//
class TA_CAP("thread") ThreadChecker final {
 public:
  explicit ThreadChecker(std::optional<std::thread::id> id) : id_(id) {}

  // Reports whether we are running on the correct thread.
  bool IsValid() const { return !id_ || std::this_thread::get_id() == *id_; }

  // Crashes if not running on the correct thread.
  void Check() const TA_ACQ() { FX_CHECK(IsValid()); }

 private:
  // This is needed to make thread analysis happy.
  friend class ScopedThreadChecker;
  void Release() const TA_REL() {}

  const std::optional<std::thread::id> id_;
};

// Allows using ThreadChecker in a scoped way that integrates with
// clang's thread safety analysis:
//
//   ThreadChecker thread_checker;
//
//   void foo() TA_REQ(thread_checker);
//   void bar() {
//     // This line will crash if not called from the correct thread.
//     ScopedThreadChecker checker(thread_checker);
//     foo();
//   }
//
class TA_SCOPED_CAP ScopedThreadChecker final {
 public:
  explicit ScopedThreadChecker(const ThreadChecker& checker) TA_ACQ(checker) : checker_(checker) {
    checker_.Check();
  }
  ~ScopedThreadChecker() TA_REL() { checker_.Release(); }

 private:
  const ThreadChecker& checker_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_THREAD_CHECKER_H_
