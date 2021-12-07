// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_TESTS_CODING_TEST_UTIL_H_
#define SRC_LIB_FIDL_CPP_TESTS_CODING_TEST_UTIL_H_

#include <lib/stdcompat/span.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <sstream>

#include <zxtest/zxtest.h>

namespace fidl_testing {

inline bool operator==(zx_handle_disposition_t a, zx_handle_disposition_t b) {
  return a.operation == b.operation && a.handle == b.handle && a.type == b.type &&
         a.rights == b.rights && a.result == b.result;
}
inline bool operator!=(zx_handle_disposition_t a, zx_handle_disposition_t b) { return !(a == b); }

inline bool operator==(fidl_channel_handle_metadata_t a, fidl_channel_handle_metadata_t b) {
  return a.obj_type == b.obj_type && a.rights == b.rights;
}
inline bool operator!=(fidl_channel_handle_metadata_t a, fidl_channel_handle_metadata_t b) {
  return !(a == b);
}

inline std::string ToString(const zx_handle_disposition_t& hd) {
  std::ostringstream os;
  os << "zx_handle_disposition_t{\n"
     << "  .operation = " << hd.operation << "\n"
     << "  .handle = " << hd.handle << "\n"
     << "  .type = " << hd.type << "\n"
     << "  .rights = " << hd.rights << "\n"
     << "  .result = " << hd.result << "\n"
     << "}\n";
  return os.str();
}

inline std::string ToString(const fidl_channel_handle_metadata_t& metadata) {
  std::ostringstream os;
  os << "fidl_channel_handle_metadata_t{\n"
     << "  .obj_type = " << metadata.obj_type << "\n"
     << "  .rights = " << metadata.rights << "\n"
     << "}\n";
  return os.str();
}

template <typename T>
void ComparePayload(const cpp20::span<T> actual, const cpp20::span<T> expected) {
  for (size_t i = 0; i < actual.size() && i < expected.size(); i++) {
    if (actual[i] != expected[i]) {
      if constexpr (std::is_same_v<T, zx_handle_disposition_t>) {
        ADD_FAILURE("element[%zu]: actual=%s, expected=%s", i, ToString(actual[i]).c_str(),
                    ToString(expected[i]).c_str());
      } else if constexpr (std::is_same_v<T, fidl_channel_handle_metadata_t>) {
        ADD_FAILURE("element[%zu]: actual=%s, expected=%s", i, ToString(actual[i]).c_str(),
                    ToString(expected[i]).c_str());
      } else {
        ADD_FAILURE("element[%zu]: actual=0x%x, expected=0x%x", i, actual[i], expected[i]);
      }
    }
  }
  EXPECT_EQ(expected.size(), actual.size(), "actual element count is different from expected");
}

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_CPP_TESTS_CODING_TEST_UTIL_H_
