// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV1_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV1_H_

#include <lib/ddk/debug.h>

namespace audio::da7219 {

// Logger/logger_ are only needed to match DFv2 libraries and share the driver server
// implementation.
#define DA7219_LOG            \
  static_cast<void>(logger_); \
  zxlogf
class Logger {};

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV1_H_
