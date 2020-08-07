// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_decoder.h"

namespace {

std::atomic<uint32_t> next_decoder_id_;

}  // namespace

VideoDecoder::VideoDecoder(Owner* owner, Client* client, bool is_secure)
    : decoder_id_(next_decoder_id_++), owner_(owner), client_(client), is_secure_(is_secure) {
  pts_manager_ = std::make_unique<PtsManager>();
}
