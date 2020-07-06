// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/options.h"

namespace tracing {

const BufferingModeSpec kBufferingModes[] = {{"oneshot", BufferingMode::kOneshot},
                                             {"circular", BufferingMode::kCircular},
                                             {"streaming", BufferingMode::kStreaming}};

const BufferingModeSpec* LookupBufferingMode(const std::string& name) {
  for (const auto& mode : kBufferingModes) {
    if (name == mode.name) {
      return &mode;
    }
  }
  return nullptr;
}

}  // namespace tracing
