// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp.h"

#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <ddk/debug.h>
#include <zxtest/zxtest.h>

#include "debug-logging.h"

namespace audio::intel_hda {
namespace {

TEST(ParseModules, TruncatedData) {
  // Try parsing a range of bytes, where the data has been truncated.
  const int kMaxDataSize = sizeof(ModulesInfo) + sizeof(ModuleEntry) - 1;
  uint8_t buff[kMaxDataSize] = {};
  for (int i = 0; i < kMaxDataSize; i++) {
    ModulesInfo info{};
    info.module_count = 1;
    memcpy(buff, &info, sizeof(ModulesInfo));
    EXPECT_TRUE(!ParseModules(fbl::Span<uint8_t>(buff)).ok());
  }
}

TEST(ParseModules, RealData) {
  struct Data {
    ModulesInfo header;
    ModuleEntry entry1;
    ModuleEntry entry2;
  } __PACKED data;

  // Generate a set of 2 modules.
  data.header.module_count = 2;
  memcpy(data.entry1.name, "ABC\0", 4);
  data.entry1.module_id = 42;
  static_assert(sizeof(data.entry2.name) == 8);
  memcpy(data.entry2.name, "01234567", 8);  // Pack the entire 8 bytes for the name.
  data.entry2.module_id = 17;

  // Parse the modules.
  auto result = ParseModules(fbl::Span(reinterpret_cast<const uint8_t *>(&data), sizeof(data)))
                    .ConsumeValueOrDie();

  // Ensure both module entries appear in the output.
  EXPECT_EQ(result.size(), 2);

  auto a = result.find("ABC");
  ASSERT_TRUE(a != result.end());
  EXPECT_EQ(a->second->module_id, 42);

  auto b = result.find("01234567");
  ASSERT_TRUE(b != result.end());
  EXPECT_EQ(b->second->module_id, 17);
}

}  // namespace
}  // namespace audio::intel_hda
