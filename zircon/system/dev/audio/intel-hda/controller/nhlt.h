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

#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/vector.h>
#include <intel-hda/utils/nhlt.h>
#include <intel-hda/utils/status.h>
#include <intel-hda/utils/status_or.h>

namespace audio::intel_hda {

// Details about an available I2S bus.
struct I2SConfig {
  I2SConfig(uint8_t bid, uint8_t dir, const formats_config_t* f)
      : valid(true), bus_id(bid), direction(dir), formats(f) {}
  I2SConfig() = default;

  bool valid = false;
  uint8_t bus_id = 0;
  uint8_t direction = 0;

  const formats_config_t* formats = nullptr;
};

// Parsed Non-HD Audio Link Table.
class Nhlt {
 public:
  // Construct an empty NHLT.
  Nhlt() = default;

  // Parse the given raw NHLT data.
  static StatusOr<std::unique_ptr<Nhlt>> FromBuffer(fbl::Array<uint8_t> buffer);

  // Parse the given raw NHLT data. The underlying data must outlive this NHLT object.
  static StatusOr<std::unique_ptr<Nhlt>> FromUnownedBuffer(uint8_t* buffer, size_t size);

  // Get parsed I2S configs.
  const fbl::Vector<I2SConfig>& i2s_configs() const { return i2s_configs_; }

  // Log debugging information about the NHLT to console.
  static void DumpNhlt(const uint8_t* data, size_t length);
  void Dump() const;

 private:
  // Objects in "i2s_configs_" point into buffer_: disallow copy and move.
  DISALLOW_COPY_ASSIGN_AND_MOVE(Nhlt);

  fbl::Array<uint8_t> buffer_;
  fbl::Vector<I2SConfig> i2s_configs_;  // Entries may point into buffer.
};

}  // namespace audio::intel_hda

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_NHLT_H_
