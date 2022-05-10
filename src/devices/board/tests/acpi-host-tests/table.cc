// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/tests/acpi-host-tests/table.h"

namespace acpi {
uint8_t ChecksumTable(void* data, size_t length) {
  uint8_t* array = static_cast<uint8_t*>(data);
  uint8_t total = 0;
  for (size_t i = 0; i < length; i++) {
    total += array[i];
  }

  // Invert because the checksum value should make (total + checksum == 0) hold.
  return -total;
}

std::vector<uint8_t> AcpiXsdt::EncodeXsdt(std::vector<uint64_t> entries) {
  this->length = static_cast<uint32_t>(sizeof(*this) + entries.size() * sizeof(uint64_t));
  std::vector<uint8_t> ret(this->length);

  this->checksum = 0;
  uint8_t sum_header = -ChecksumTable(this, sizeof(*this));
  uint8_t sum_entries = -ChecksumTable(entries.data(), sizeof(uint64_t) * entries.size());
  sum_header += sum_entries;
  this->checksum = -sum_header;

  memcpy(ret.data(), this, sizeof(*this));
  memcpy(ret.data() + sizeof(*this), entries.data(), entries.size() * sizeof(uint64_t));
  return ret;
}

}  // namespace acpi
