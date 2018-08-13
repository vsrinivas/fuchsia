// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/types/bytes.h"

namespace media_player {

Bytes::Bytes(size_t size) : storage_(size) {}

Bytes::~Bytes() {}

std::unique_ptr<Bytes> Bytes::Clone() const {
  return std::unique_ptr<Bytes>(new Bytes(*this));
}

}  // namespace media_player
