// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-header.h>

#include <memory>

namespace nand_rs {

NandRsHeader MakeHeader(const std::vector<uint8_t>& buffer) {
  NandRsHeader header;
  memcpy(header.magic, kNandRsMagic, kNandRsMagicSize);
  header.crc = crc32(0, buffer.data(), buffer.size());
  header.file_size = static_cast<uint32_t>(buffer.size());
  return header;
}

std::optional<NandRsHeader> ReadHeader(const std::vector<uint8_t>& buffer, uint32_t block_size) {
  NandRsHeader header;
  memcpy(&header, buffer.data(), kNandRsHeaderSize);

  if (strncmp(header.magic, kNandRsMagic, kNandRsMagicSize) != 0) {
    return std::nullopt;
  }

  if (header.file_size == 0 || header.file_size > block_size - kNandRsHeaderSize) {
    return std::nullopt;
  }

  if (header.crc != crc32(0, buffer.data() + kNandRsHeaderSize, header.file_size)) {
    return std::nullopt;
  }

  return header;
}

}  // namespace nand_rs
