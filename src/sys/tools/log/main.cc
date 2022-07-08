// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

int main(int argc, char** argv) {
  syslog::SetLogSettings(syslog::LogSettings{
      .disable_interest_listener = true,
  });
  if (argc != 3) {
    std::cerr << "Usage: log [tag] [message]" << std::endl;
    return -1;
  }
  std::string tag = argv[1];
  std::string message = argv[2];

  if (tag.length() > fuchsia::logger::MAX_TAG_LEN_BYTES) {
    std::cerr << "Tag too long." << std::endl;
    return -1;
  }

  if (tag.length() + 1 + 1 + message.length() + 1 > fuchsia::logger::MAX_DATAGRAM_LEN_BYTES) {
    std::cerr << "Message too long." << std::endl;
    return -1;
  }

  FX_LOGST(INFO, tag.c_str()) << message;

  return 0;
}
