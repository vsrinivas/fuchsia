// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/comparison.h"

#include <map>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/clone.h"

#ifdef __Fuchsia__
#include <lib/zx/event.h>
#endif

namespace fidl {
namespace {

TEST(Comparison, UnknownBytes) {
  UnknownBytes bytes1 = {
      .bytes = {0xde, 0xad, 0xbe, 0xef},
  };

  UnknownBytes bytes2 = {
      .bytes = {0xde, 0xad, 0xbe, 0xef},
  };

  ASSERT_EQ(true, Equals(bytes1, bytes2));
}

#ifdef __Fuchsia__
TEST(Comparison, UnknownDataSameHandle) {
  zx_handle_t h;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h));
  UnknownData data1 = {
      .bytes = {0xde, 0xad},
      .handles = {},
  };
  data1.handles.push_back(zx::handle(h));

  UnknownData data2 = {
      .bytes = {0xde, 0xad},
      .handles = {},
  };
  data2.handles.push_back(zx::handle(h));

  ASSERT_EQ(true, Equals(data1, data2));

  std::map<uint64_t, UnknownData> map1;
  map1.insert({3, std::move(data1)});
  std::map<uint64_t, UnknownData> map2;
  map2.insert({3, std::move(data2)});
  ASSERT_EQ(true, Equals(map1, map2));

  // prevent double close by releasing one of the handles
  [[maybe_unused]] zx_handle_t copy = map1.begin()->second.handles[0].release();
}

TEST(Comparison, UnknownDataCopiedHandle) {
  zx_handle_t h;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h));
  UnknownData data1 = {
      .bytes = {0xde, 0xad},
      .handles = {},
  };
  data1.handles.push_back(zx::handle(h));

  UnknownData data2;
  ASSERT_EQ(ZX_OK, Clone(data1, &data2));

  ASSERT_EQ(false, Equals(data1, data2));

  std::map<uint64_t, UnknownData> map1;
  map1.insert({3, std::move(data1)});
  std::map<uint64_t, UnknownData> map2;
  map2.insert({3, std::move(data2)});
  ASSERT_EQ(false, Equals(map1, map2));
}
#endif

}  // namespace
}  // namespace fidl
