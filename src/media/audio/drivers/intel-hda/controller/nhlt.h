// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides functionality for parsing the system's Non-HD Audio Link
// Table (NHLT), which in turn provides details about the system's audio
// capabilities.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_NHLT_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_NHLT_H_

#include <zircon/compiler.h>

#include <cstdint>
#include <ostream>

#include <fbl/macros.h>
#include <fbl/span.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>
#include <intel-hda/utils/status.h>
#include <intel-hda/utils/status_or.h>

namespace audio::intel_hda {

// Details about an available I2S bus.
struct I2SConfig {
  struct Format {
    format_config_t config;
    fbl::Vector<uint8_t> capabilities;
  };
  nhlt_descriptor_t header = {};
  uint8_t bus_id = 0;
  uint8_t direction = 0;
  fbl::Vector<Format> formats;
  fbl::Vector<uint8_t> specific_config;
};

// Parsed Non-HD Audio Link Table.
class Nhlt {
 public:
  // Construct an empty NHLT.
  Nhlt() = default;

  // Parse the given raw NHLT data.
  static StatusOr<std::unique_ptr<Nhlt>> FromBuffer(fbl::Span<const uint8_t> buffer);

  // Get parsed I2S configs.
  const fbl::Vector<I2SConfig>& i2s_configs() const { return i2s_configs_; }

  // Log debugging information about the NHLT to console.
  static void DumpNhlt(const uint8_t* data, size_t length);
  void Dump() const;

 private:
  fbl::Vector<I2SConfig> i2s_configs_;
};

}  // namespace audio::intel_hda

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_NHLT_H_
