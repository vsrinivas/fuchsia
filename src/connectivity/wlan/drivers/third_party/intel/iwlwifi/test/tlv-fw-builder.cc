// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/tlv-fw-builder.h"

#include <endian.h>

#include <cstring>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/file.h"
}  // extern "C"

namespace wlan::testing {

TlvFwBuilder::TlvFwBuilder() {
  // Build a minimal TLV firmware header.
  static constexpr char kFwName[] = "fuchsia_debug_fw";
  static constexpr int kFwApiVersion = 17;
  iwl_tlv_ucode_header header = {};
  header.zero = 0;
  header.magic = htole32(IWL_TLV_UCODE_MAGIC);
  std::memcpy(header.human_readable, kFwName, sizeof(kFwName));
  header.ver = (kFwApiVersion << 8);  // See: IWL_UCODE_API.
  header.build = 0;
  binary_.resize(sizeof(header), '\0');
  std::memcpy(binary_.data(), &header, sizeof(header));
}

void TlvFwBuilder::AddValue(uint32_t type, const void* data, size_t size) {
  iwl_ucode_tlv tlv = {};
  tlv.type = cpu_to_le32(type);
  tlv.length = cpu_to_le32(size);

  const size_t data_size = (size + 3) & ~3;
  const size_t old_size = binary_.size();
  binary_.resize(binary_.size() + sizeof(tlv) + data_size, '\0');
  char* fw_data = binary_.data() + old_size;

  std::memcpy(fw_data, &tlv, sizeof(tlv));
  fw_data += sizeof(tlv);
  std::memcpy(fw_data, data, size);
}

TlvFwBuilder::~TlvFwBuilder() = default;

std::string TlvFwBuilder::GetBinary() const { return binary_; }

}  // namespace wlan::testing
