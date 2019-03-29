// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_CODEC_OUTPUT_H_
#define GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_CODEC_OUTPUT_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <src/lib/fxl/macros.h>
#include <memory>

// Each CodecOutput represents a Packet, and the correct associated
// StreamOutputConfig for that packet.  Since the CodecClient takes care of
// buffer_constraints_action_required true internally, the consumer of
// CodecOutput never has to deal with the situation where there's a new buffer
// constraints that's action required before any more output packets will show
// up.  That's dealt with by the time CodecOutput is created.
//
// While we could have written this example to deal with output packets and
// output config changes one at a time directly on the FIDL thread, that's
// actually not quite as instructive as this example in my view, because it
// would make the ordering aspect less explicit.
class CodecOutput {
 public:
  CodecOutput(
      uint64_t stream_lifetime_ordinal,
      std::shared_ptr<const fuchsia::media::StreamOutputConfig> config,
      std::unique_ptr<const fuchsia::media::Packet> packet,
      bool end_of_stream);

  uint64_t stream_lifetime_ordinal() { return stream_lifetime_ordinal_; }

  std::shared_ptr<const fuchsia::media::StreamOutputConfig> config() {
    // Caller should only call this after checking end_of_stream() first.
    assert(config_);
    return config_;
  }

  // The caller doesn't own the returned reference, and the caller must ensure
  // the returned reference isn't retained beyond the lifetime of CodecOutput.
  const fuchsia::media::Packet& packet() {
    // Caller should only call this after checking end_of_stream() first.
    assert(packet_);
    return *packet_;
  }

  bool end_of_stream() { return end_of_stream_; }

 private:
  uint64_t stream_lifetime_ordinal_ = 0;
  // The shared_ptr<> is just to optimize away copying an immutable config.
  std::shared_ptr<const fuchsia::media::StreamOutputConfig> config_;
  std::unique_ptr<const fuchsia::media::Packet> packet_;

  bool end_of_stream_ = false;

  // TODO(dustingreen): put this on other classes in this file also.
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecOutput);
};

#endif  // GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_CODEC_OUTPUT_H_
