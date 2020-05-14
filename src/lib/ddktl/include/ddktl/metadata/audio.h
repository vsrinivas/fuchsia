// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_

namespace metadata {

enum class Codec : uint32_t {
  Tas5782,
  Tas5805,
  Tas5720x3,
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_AUDIO_H_
