// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_UTIL_DEBUG_H_
#define PERIDOT_LIB_UTIL_DEBUG_H_

#include <ostream>
#include <string>

namespace modular {

// When debugging multiple copies of an object (such as Links), this creates a
// printable id to disambiguate them
inline std::string GetDebugId(void* p) {
  std::ostringstream stream;
  stream << std::hex << ((uint64_t)p & 0x0fff) << " ";
  return stream.str();
}

}  // namespace modular

#endif  // PERIDOT_LIB_UTIL_DEBUG_H_
