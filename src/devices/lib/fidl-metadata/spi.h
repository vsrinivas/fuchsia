// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_FIDL_METADATA_SPI_H_
#define SRC_DEVICES_LIB_FIDL_METADATA_SPI_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

namespace fidl_metadata::spi {
struct Channel {
  uint32_t bus_id;
  uint32_t cs;

  uint32_t vid;
  uint32_t pid;
  uint32_t did;
};

// Convert an array of spi_channel to fuchsia.hardware.spi.SpiBusMetadata encoded
// in a FIDL bytestream.
zx::status<std::vector<uint8_t>> SpiChannelsToFidl(cpp20::span<const Channel> channels);

}  // namespace fidl_metadata::spi

#endif  // SRC_DEVICES_LIB_FIDL_METADATA_SPI_H_
