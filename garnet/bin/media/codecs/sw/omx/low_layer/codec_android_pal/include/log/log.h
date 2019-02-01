// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_

// Evaluate the arguments (out of paranoia), but do nothing with this, for now.
#define ALOGE(x) \
  do {           \
    (void)(x);   \
  } while (0)

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_LOW_LAYER_CODEC_ANDROID_PAL_INCLUDE_LOG_LOG_H_
