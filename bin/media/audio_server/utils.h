// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <atomic>

#include "garnet/bin/media/audio_server/constants.h"

namespace media {
namespace audio {

class GenerationId {
 public:
  uint32_t get() const { return id_; }
  uint32_t Next() {
    uint32_t ret;
    do {
      ret = ++id_;
    } while (ret == kInvalidGenerationId);
    return ret;
  }

 private:
  uint32_t id_ = kInvalidGenerationId + 1;
};

class AtomicGenerationId {
 public:
  AtomicGenerationId() : id_(kInvalidGenerationId + 1) {}

  uint32_t get() const { return id_.load(); }
  uint32_t Next() {
    uint32_t ret;
    do {
      ret = id_.fetch_add(1);
    } while (ret == kInvalidGenerationId);
    return ret;
  }

 private:
  std::atomic<uint32_t> id_;
};

}  // namespace audio
}  // namespace media
