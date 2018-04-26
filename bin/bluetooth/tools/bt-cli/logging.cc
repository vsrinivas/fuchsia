// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging.h"

namespace bluetoothcli {

LogMessage::LogMessage(size_t indent_count) {
  // All output is indented by 2 spaces plus any requested indent count
  std::cout << std::string(2 + indent_count * 2, ' ');
}

LogMessage::~LogMessage() {
  std::cout << std::endl;
}

}  // namespace bluetoothcli
