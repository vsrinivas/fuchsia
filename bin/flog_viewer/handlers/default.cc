// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/default.h"

#include <iomanip>
#include <iostream>

#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/formatting.h"

namespace mojo {
namespace flog {
namespace handlers {

Default::Default(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse),
      full_(format == FlogViewer::kFormatFull) {}

Default::~Default() {}

void Default::HandleMessage(Message* message) {
  if (terse_ || full_) {
    std::cout << entry() << "channel message, size "
              << message->data_num_bytes() << " name " << message->name()
              << std::endl;
    if (full_) {
      PrintData(message->data(), message->data_num_bytes());
    }
  }
}

// static
void Default::PrintData(const uint8_t* data, size_t size) {
  size_t line_offset = 0;
  while (true) {
    std::cout << "    " << std::hex << std::setw(4) << line_offset << " ";

    std::string chars(kDataBytesPerLine, ' ');

    for (size_t i = 0; i < kDataBytesPerLine; ++i) {
      if (i == kDataBytesPerLine / 2) {
        std::cout << " ";
      }

      if (i >= size) {
        std::cout << "   ";
        ++data;
      } else {
        std::cout << " " << std::hex << std::setw(2)
                  << static_cast<uint16_t>(*data);
        if (*data >= ' ' && *data <= '~') {
          chars[i] = *data;
        } else {
          chars[i] = '.';
        }
        ++data;
      }
    }

    std::cout << "  " << chars << std::endl;

    if (size <= kDataBytesPerLine) {
      break;
    }
    line_offset += kDataBytesPerLine;
    size -= kDataBytesPerLine;
  }
}

}  // namespace handlers
}  // namespace flog
}  // namespace mojo
