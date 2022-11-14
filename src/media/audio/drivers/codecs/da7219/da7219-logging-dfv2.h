// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV2_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV2_H_

#include <lib/driver/component/cpp/driver_cpp.h>

namespace audio::da7219 {

// Indirection used for logging. Used to match DFv2 libraries and DFv1, could be removed after
// DFv1 is no longer supported, see da7219-logging-dfv1.h.
#define DA7219_LOG FDF_LOG
#define Logger ::driver::Logger

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_LOGGING_DFV2_H_
