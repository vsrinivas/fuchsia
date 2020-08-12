// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_

namespace metadata {

static constexpr uint32_t kMaxNumberOfCodecs = 8;
static constexpr uint32_t kMaxNumberOfExternalDelays = 8;

enum class Codec : uint32_t {
  Tas27xx,
  Tas5782,
  Tas58xx,
  Tas5720,
};

enum class TdmType : uint32_t {
  I2s,
  LeftJustified,
  Pcm,
};

struct ExternalDelay {
  uint32_t frequency;
  int64_t nsecs;
};

struct Tdm {
  TdmType type;
  uint32_t number_of_codecs;
  Codec codecs[kMaxNumberOfCodecs];
  float codecs_delta_gains[kMaxNumberOfCodecs];
  uint32_t number_of_external_delays;
  ExternalDelay external_delays[kMaxNumberOfExternalDelays];
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
