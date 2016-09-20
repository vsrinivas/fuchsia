// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_decoder_full.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_decoder_channel.mojom.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace mojo {
namespace flog {
namespace handlers {

MediaDecoderFull::MediaDecoderFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaDecoderFull::~MediaDecoderFull() {}

void MediaDecoderFull::HandleMessage(Message* message) {
  stub_.Accept(message);
}

void MediaDecoderFull::Config(mojo::media::MediaTypePtr input_type,
                              mojo::media::MediaTypePtr output_type,
                              uint64_t consumer_address,
                              uint64_t producer_address) {
  std::cout << entry() << "MediaDecoder.Config" << std::endl;
  std::cout << indent;
  std::cout << begl << "input_type: " << input_type;
  std::cout << begl << "output_type: " << output_type;
  std::cout << begl << "consumer_address: " << *AsChannel(consumer_address)
            << std::endl;
  std::cout << begl << "producer_address: " << *AsChannel(producer_address)
            << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
}  // namespace mojo
