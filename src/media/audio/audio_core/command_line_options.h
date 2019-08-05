// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_COMMAND_LINE_OPTIONS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_COMMAND_LINE_OPTIONS_H_

#include <lib/fit/promise.h>
#include <zircon/types.h>

#include "src/lib/fxl/command_line.h"

namespace media::audio {

struct CommandLineOptions {
  static fit::result<CommandLineOptions, zx_status_t> ParseFromArgcArgv(int argc,
                                                                        const char** argv);

  bool enable_device_settings_writeback = true;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_COMMAND_LINE_OPTIONS_H_
