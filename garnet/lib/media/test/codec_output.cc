// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/test/codec_output.h>

#include <stdint.h>
#include <memory>

CodecOutput::CodecOutput(
    uint64_t stream_lifetime_ordinal,
    std::shared_ptr<const fuchsia::media::StreamOutputConstraints> constraints,
    std::shared_ptr<const fuchsia::media::StreamOutputFormat> format,
    std::unique_ptr<const fuchsia::media::Packet> packet,
    bool end_of_stream)
    : stream_lifetime_ordinal_(stream_lifetime_ordinal),
      constraints_(constraints),
      format_(format),
      packet_(std::move(packet)),
      end_of_stream_(end_of_stream) {
  // nothing else to do here
}
