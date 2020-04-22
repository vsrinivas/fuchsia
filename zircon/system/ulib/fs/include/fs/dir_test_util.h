// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_DIR_TEST_UTIL_H_
#define FS_DIR_TEST_UTIL_H_

#include <lib/fdio/vfs.h>

#include <zxtest/zxtest.h>

namespace fs {
// Helper class to check entries of a directory
// Usage example:
//    fs::PseudoDir* test; // Test directory has SampleDir and SampleFile.
//    uint8_t buffer[256];
//    size_t len;
//    EXPECT_EQ(test->Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
//    fs::DirentChecker dc(buffer, len);
//    dc.ExpectEntry(".", V_TYPE_DIR);
//    dc.ExpectEntry("SampleDir", V_TYPE_DIR);
//    dc.ExpectEntry("SampleFile",V_TYPE_FILE);
//    dc.ExpectEnd();
//
class DirentChecker {
 public:
  DirentChecker(const uint8_t* buffer, size_t length)
      : current_(reinterpret_cast<const uint8_t*>(buffer)), remaining_(length) {}

  void ExpectEnd() { EXPECT_EQ(0u, remaining_); }

  void ExpectEntry(const char* name, uint32_t vtype) {
    ASSERT_NE(0u, remaining_);
    auto entry = reinterpret_cast<const vdirent_t*>(current_);
    size_t entry_size = entry->size + sizeof(vdirent_t);
    ASSERT_GE(remaining_, entry_size);
    current_ += entry_size;
    remaining_ -= entry_size;
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(name),
                    reinterpret_cast<const uint8_t*>(entry->name), strlen(name), "name");
    EXPECT_EQ(VTYPE_TO_DTYPE(vtype), entry->type);
  }

 private:
  const uint8_t* current_;
  size_t remaining_;
};
}  // namespace fs

#endif  // FS_DIR_TEST_UTIL_H_
