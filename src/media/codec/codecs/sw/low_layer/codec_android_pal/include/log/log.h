// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_SW_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_
#define SRC_MEDIA_CODEC_CODECS_SW_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_

// Evaluate the arguments (out of paranoia), but do nothing with this, for now.
#define ALOGE(x) \
  do {           \
    (void)(x);   \
  } while (0)

#endif  // SRC_MEDIA_CODEC_CODECS_SW_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_
