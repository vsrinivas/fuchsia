// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garent/bin/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for otherwise unhandled messages.
class Default : public ChannelHandler {
 public:
  static const size_t kDataBytesPerLine = 16;

  // Print a hex dump of the indicated data.
  static void PrintData(const uint8_t* data, size_t size);

  Default(const std::string& format);

  ~Default() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;
};

}  // namespace handlers
}  // namespace flog
