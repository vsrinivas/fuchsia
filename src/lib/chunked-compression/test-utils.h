/// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_
#define SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_

#include <stdint.h>

#include <initializer_list>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>

namespace chunked_compression {
namespace test_utils {

// Computes the checksum for a raw archive header.
// |header_length| must be at least kChunkArchiveMinHeaderSize bytes.
uint32_t ComputeChecksum(const uint8_t* header, size_t header_length);

// Serializes a well formed seek table with |entries| to a buffer.
// The seek table invariants are *NOT* checked. This is intentional to catch
// HeaderReader::Parse bugs in the test cases.
fbl::Array<uint8_t> CreateHeader(std::initializer_list<SeekTableEntry> entries);

inline fbl::Array<uint8_t> CreateHeader() { return CreateHeader({}); }

}  // namespace test_utils
}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_
