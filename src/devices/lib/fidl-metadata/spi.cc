// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/lib/fidl-metadata/spi.h"

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>

namespace fidl_metadata::spi {
zx::status<std::vector<uint8_t>> SpiChannelsToFidl(const cpp20::span<const Channel> channels) {
  fidl::Arena allocator;
  fidl::VectorView<fuchsia_hardware_spi::wire::SpiChannel> spi_channels(allocator, channels.size());

  for (size_t i = 0; i < channels.size(); i++) {
    auto& chan = spi_channels[i];
    chan.Allocate(allocator);

    chan.set_bus_id(channels[i].bus_id);
    chan.set_cs(channels[i].cs);
    chan.set_pid(channels[i].pid);
    chan.set_did(channels[i].did);
    chan.set_vid(channels[i].vid);
  }

  fuchsia_hardware_spi::wire::SpiBusMetadata metadata(allocator);
  metadata.set_channels(allocator, spi_channels);

  fidl::unstable::OwnedEncodedMessage<fuchsia_hardware_spi::wire::SpiBusMetadata> encoded(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded.ok()) {
    return zx::error(encoded.status());
  }

  auto message = encoded.GetOutgoingMessage().CopyBytes();
  std::vector<uint8_t> result(message.size());
  memcpy(result.data(), message.data(), message.size());
  return zx::ok(std::move(result));
}
}  // namespace fidl_metadata::spi
