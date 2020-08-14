// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/sys/tools/log/log.h"

#include <lib/syslog/wire_format.h>
#include <zircon/errors.h>

#include <iostream>

namespace log {

zx_status_t ParseAndWriteLog(fuchsia::logger::LogSinkHandle log_sink, zx::time time, int argc,
                             char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: log [tag] [message]" << std::endl;
    return ZX_ERR_INVALID_ARGS;
  }
  std::string tag = argv[1];
  std::string message = argv[2];

  if (tag.length() > fuchsia::logger::MAX_TAG_LEN_BYTES) {
    std::cerr << "Tag too long." << std::endl;
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (tag.length() + 1 + 1 + message.length() + 1 > sizeof(fx_log_packet::data)) {
    std::cerr << "Message too long." << std::endl;
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx::socket client;
  zx::socket server;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &client, &server);
  if (status != ZX_OK) {
    std::cerr << "Failed to create socket." << std::endl;
    return status;
  }

  status = log_sink.BindSync()->Connect(std::move(server));
  if (status != ZX_OK) {
    std::cerr << "Failed to connect socket." << std::endl;
    return status;
  }

  fx_log_packet packet{
      .metadata{.time = time.get(), .severity = fuchsia::logger::LOG_LEVEL_DEFAULT}};
  packet.data[0] = tag.length();
  tag.copy(&packet.data[1], tag.length());
  message.copy(&packet.data[tag.length() + 2], message.length());
  size_t actual = 0;
  status = client.write(0, &packet, sizeof(packet), &actual);
  if (status != ZX_OK || actual != sizeof(packet)) {
    std::cerr << "Failed to write data to socket." << std::endl;
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  return ZX_OK;
}

}  // namespace log
