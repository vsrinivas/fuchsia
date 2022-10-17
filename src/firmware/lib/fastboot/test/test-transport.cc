// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fastboot/fastboot_base.h>
#include <lib/fastboot/test/test-transport.h>

#include <string>
#include <vector>

namespace fastboot {

void TestTransport::AddInPacket(const void* data, size_t size) {
  const char* start = static_cast<const char*>(data);
  in_packets_.insert(in_packets_.begin(), std::string(start, start + size));
}

zx::result<size_t> TestTransport::ReceivePacket(void* dst, size_t capacity) {
  if (in_packets_.empty()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  const std::string& packet = in_packets_.back();
  if (packet.size() > capacity) {
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  size_t size = packet.size();
  memcpy(dst, packet.data(), size);
  in_packets_.pop_back();
  return zx::ok(size);
}

// Send a packet over the transport.
zx::result<> TestTransport::Send(std::string_view packet) {
  out_packets_.push_back(std::string(packet.data(), packet.size()));
  return zx::ok();
}

}  // namespace fastboot
