// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/media/demux/demux.h"

namespace media {

class FfmpegDemux : public Demux {
 public:
  static std::shared_ptr<Demux> Create(std::shared_ptr<Reader> reader);
};

}  // namespace media
