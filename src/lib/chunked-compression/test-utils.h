/// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_
#define SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_

namespace chunked_compression {

// Serializes a well formed seek table with |entries| to a buffer.
// The seek table invariants are *NOT* checked. This is intentional to catch
// HeaderReader::Parse bugs in the test cases.
fbl::Array<uint8_t> CreateHeader(std::initializer_list<SeekTableEntry> entries);

inline fbl::Array<uint8_t> CreateHeader() { return CreateHeader({}); }

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_TEST_UTILS_H_
