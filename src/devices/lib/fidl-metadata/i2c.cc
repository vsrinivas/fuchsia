// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/lib/fidl-metadata/i2c.h"

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>

namespace fidl_metadata::i2c {
zx::status<std::vector<uint8_t>> I2CChannelsToFidl(const cpp20::span<const Channel> channels) {
  fidl::Arena allocator;
  fidl::VectorView<fuchsia_hardware_i2c::wire::I2CChannel> i2c_channels(allocator, channels.size());

  for (size_t i = 0; i < channels.size(); i++) {
    auto& chan = i2c_channels[i];
    auto& src_chan = channels[i];
    chan.Allocate(allocator);

    chan.set_bus_id(src_chan.bus_id);
    chan.set_address(src_chan.address);
    if (src_chan.pid || src_chan.vid || src_chan.did) {
      chan.set_pid(src_chan.pid);
      chan.set_did(src_chan.did);
      chan.set_vid(src_chan.vid);
    }
  }

  fuchsia_hardware_i2c::wire::I2CBusMetadata metadata(allocator);
  metadata.set_channels(allocator, i2c_channels);

  fidl::OwnedEncodedMessage<fuchsia_hardware_i2c::wire::I2CBusMetadata> encoded(&metadata);
  if (!encoded.ok()) {
    return zx::error(encoded.status());
  }

  auto message = encoded.GetOutgoingMessage().CopyBytes();
  std::vector<uint8_t> result(message.size());
  memcpy(result.data(), message.data(), message.size());
  return zx::ok(std::move(result));
}
}  // namespace fidl_metadata::i2c
