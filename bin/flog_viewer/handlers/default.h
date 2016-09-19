// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_DEFAULT_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_DEFAULT_H_

#include "examples/flog_viewer/channel_handler.h"

namespace mojo {
namespace flog {
namespace examples {
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
  void HandleMessage(Message* message) override;

 private:
  bool terse_;
  bool full_;
};

}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_DEFAULT_H_
