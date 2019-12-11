// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_INPUT_COPIER_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_INPUT_COPIER_H_

#include <lib/zx/vmo.h>

#include <memory>

class InputCopier {
 public:
  static std::unique_ptr<InputCopier> Create();

  virtual ~InputCopier() = default;

  // Get how much longer output is than input.
  virtual uint32_t PaddingLength() const = 0;
  // Copy data into a secure VMO.
  // Returns TEE result (negative on failure).
  virtual int DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) = 0;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_INPUT_COPIER_H_
