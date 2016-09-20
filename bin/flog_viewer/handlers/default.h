// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_DEFAULT_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_DEFAULT_H_

#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace mojo {
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
  void HandleMessage(Message* message) override;

 private:
  bool terse_;
  bool full_;
};

}  // namespace handlers
}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_TOOLS_FLOG_VIEWER_HANDLERS_DEFAULT_H_
