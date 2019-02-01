// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_ARRAY_SIZE_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_ARRAY_SIZE_H_

// Only compiles for arrays with known size.
template <typename T, size_t N>
constexpr size_t array_size(T (&)[N]) {
  return N;
}

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_ARRAY_SIZE_H_
