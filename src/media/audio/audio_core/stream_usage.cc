// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_usage.h"

namespace media::audio {

const char* RenderUsageToString(const RenderUsage& usage) {
  switch (usage) {
#define EXPAND_RENDER_USAGE(U) \
  case RenderUsage::U:         \
    return "RenderUsage::" #U;
    EXPAND_EACH_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
  }
}

const char* CaptureUsageToString(const CaptureUsage& usage) {
  switch (usage) {
#define EXPAND_CAPTURE_USAGE(U) \
  case CaptureUsage::U:         \
    return "CaptureUsage::" #U;
    EXPAND_EACH_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
  }
}

const char* StreamUsage::ToString() const {
  if (is_render_usage()) {
    return RenderUsageToString(render_usage());
  } else if (is_capture_usage()) {
    return CaptureUsageToString(capture_usage());
  } else {
    return "(empty usage)";
  }
}

}  // namespace media::audio
