// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides functionality for parsing the system's Non-HD Audio Link
// Table (NHLT), which in turn provides details about the system's audio
// capabilities.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_NHLT_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_NHLT_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstring>
#include <ostream>

#include <fbl/macros.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>

namespace audio::intel_hda {

// Details about an available I2S bus.
struct EndPointConfig {
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
  static zx::result<std::unique_ptr<Nhlt>> FromBuffer(cpp20::span<const uint8_t> buffer);

  // Get parsed I2S configs.
  const fbl::Vector<EndPointConfig>& configs() const { return configs_; }

  bool IsOemMatch(std::string_view oem_id, std::string_view oem_table_id) const {
    return oem_id == oem_id_ && oem_table_id == oem_table_id_;
  }

  // Log debugging information about the NHLT to console.
  static void DumpNhlt(const uint8_t* data, size_t length);
  void Dump() const;

 private:
  fbl::Vector<EndPointConfig> configs_;
  std::string oem_id_;
  std::string oem_table_id_;
};

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_NHLT_H_
