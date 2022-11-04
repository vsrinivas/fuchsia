// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/verbose_log.h"

namespace media::audio {

namespace internal {
thread_local DeferredLogBuffer* const deffered_log = new DeferredLogBuffer;

void DeferredLogBuffer::Dump() {
  std::ostringstream os;
  for (size_t k = 0; k < kLines; k++) {
    auto str = lines[(next_log_idx + k) % kLines].str();
    // It would be nicer to write all of these lines to a single buffer to avoid
    // the logging prefix on each line, however large buffers tend to exceed a
    // maximum line length in archivist, so instead write each line as a separate
    // log statement.
    if (!str.empty()) {
      FX_LOGS(VERBOSE_LOGS_LEVEL) << str;
    }
  }
}
}  // namespace internal

void DumpVerboseLogs() { internal::deffered_log->Dump(); }

}  // namespace media::audio
