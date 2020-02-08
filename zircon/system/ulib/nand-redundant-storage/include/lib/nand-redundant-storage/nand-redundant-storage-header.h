// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_HEADER_H_
#define LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_HEADER_H_

#include <memory>
#include <optional>
#include <vector>

namespace nand_rs {

constexpr const char kNandRsMagic[] = "ZNND";
constexpr uint32_t kNandRsMagicSize = sizeof(kNandRsMagic) - 1;

struct NandRsHeader {
  char magic[kNandRsMagicSize];
  // CRC-32 of the file contents.
  uint32_t crc;
  // Size of the file.
  uint32_t file_size;
};

constexpr uint32_t kNandRsHeaderSize = sizeof(NandRsHeader);
static_assert(kNandRsHeaderSize == 3 * sizeof(uint32_t));

// Creates the NandRsHeader for a given |buffer|.
//
// Writes expected magic and calculates crc.
//
// Header should be written as the first |kNandRsHeaderSize| byes of a
// storage device.
NandRsHeader MakeHeader(const std::vector<uint8_t>& buffer);

// Attempts to read the header from the first |kNandRsHeaderSize| bytes in
// |buffer|.
//
// Fails if the magic is wrong, the crc is invalid or size is larger than
// the expected block size.
std::optional<NandRsHeader> ReadHeader(const std::vector<uint8_t>& buffer, uint32_t block_size);

}  // namespace nand_rs

#endif  // LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_HEADER_H_
