// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_VERBOSE_LOG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_VERBOSE_LOG_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

namespace media::audio {

namespace internal {
struct DeferredLogBuffer {
  static constexpr size_t kLines = 200;

  std::vector<std::ostringstream> lines{DeferredLogBuffer::kLines};
  size_t next_log_idx = 0;

  void Add(std::ostringstream&& os) {
    lines[next_log_idx] = std::move(os);
    if (++next_log_idx >= DeferredLogBuffer::kLines) {
      next_log_idx %= DeferredLogBuffer::kLines;
    }
  }

  void Dump();
};

extern thread_local DeferredLogBuffer* const deffered_log;

struct DeferredLogLine : public std::ostringstream {
  ~DeferredLogLine() { deffered_log->Add(std::move(*this)); }
};
}  // namespace internal

// The VERBOSE_LOGS macro is like FX_LOGS, but has two controls:
//
// - Control the logging level
// - Control whether logging is deferred (captured in a thread-local ring buffer) or emit eagerly
//
// Production builds should not disable deferred logging unless VERBOSE_LOGS_LEVEL < INFO.
#define VERBOSE_LOGS_LEVEL INFO
#define VERBOSE_LOGS_ENABLE_DEFERRED_LOGGING 1

#if VERBOSE_LOGS_ENABLE_DEFERRED_LOGGING
#define VERBOSE_LOGS ::media::audio::internal::DeferredLogLine()
// Redefine FX_CHECK to DumpVerboseLogs() on failure.
#undef FX_CHECK
#define FX_CHECK(cond)                     \
  if (unlikely(!(cond)) && ({              \
        ::media::audio::DumpVerboseLogs(); \
        true;                              \
      }))                                  \
  FX_CHECKT(cond, nullptr)

#else
#define VERBOSE_LOGS FX_LOGS(VERBOSE_LOGS_LEVEL)

#endif  // VERBOSE_LOGS_ENABLE_DEFERRED_LOGGING

// Dump our thread-local ring buffer.
void DumpVerboseLogs();

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_VERBOSE_LOG_H_
