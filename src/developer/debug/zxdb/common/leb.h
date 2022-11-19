// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LEB_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LEB_H_

#include <stdint.h>

#include <vector>

namespace zxdb {

// Appends the DWARWF unsigned "LEB128"-encoded value to the vector. This encoding is a UTF-8-like
// variable-length integer encoding.
//
// To decode, see DataExtractor::ReadUleb128();
void AppendULeb(uint64_t value, std::vector<uint8_t>* out);

// A signed version hasn't been implemented but it could go here. That is a little bit more
// complicated because it needs to account for sign-extension.

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LEB_H_
