// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_

namespace metadata {

enum class Codec : uint32_t {
  None,
  Tas27xx,
  Tas5782,
  Tas5805,
  Tas5720x3,
};

enum class TdmType : uint32_t {
  I2s,
  Pcm,
};

struct Tdm {
  TdmType type;
  Codec codec;
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
