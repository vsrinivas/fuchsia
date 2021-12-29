// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/fake-dpcd-channel.h"

namespace i915 {
namespace testing {

void FakeDpcdChannel::SetDefaults() {
  SetDpcdRevision(dpcd::Revision::k1_4);
  SetSinkCount(kDefaultSinkCount);
  SetMaxLaneCount(kDefaultLaneCount);
  SetMaxLinkRate(dpcd::LinkBw::k1620Mbps);
}

void FakeDpcdChannel::PopulateLinkRateTable(std::vector<uint16_t> values) {
  std::memset(registers.data() + dpcd::DPCD_SUPPORTED_LINK_RATE_START, 0,
              kMaxLinkRateTableEntries * 2);
  for (unsigned i = 0; i < values.size() && i < kMaxLinkRateTableEntries; i++) {
    unsigned offset = dpcd::DPCD_SUPPORTED_LINK_RATE_START + (i * 2);
    registers[offset] = static_cast<uint8_t>(values[i] & 0xFF);
    registers[offset + 1] = (values[i] >> 8);
  }
}

bool FakeDpcdChannel::DpcdRead(uint32_t addr, uint8_t* buf, size_t size) {
  if (addr + size > registers.size()) {
    return false;
  }

  std::memcpy(buf, registers.data() + addr, size);
  return true;
}

bool FakeDpcdChannel::DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size) {
  if (addr + size > registers.size()) {
    return false;
  }

  std::memcpy(registers.data() + addr, buf, size);
  return true;
}

}  // namespace testing
}  // namespace i915
