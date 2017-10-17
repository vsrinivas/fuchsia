// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/types.h>

namespace examples {

class WavHeader {
 public:
  static zx_status_t Write(int fd,
                           uint32_t channel_cnt,
                           uint32_t frame_rate,
                           size_t payload_len);
};

}  // namespace examples
