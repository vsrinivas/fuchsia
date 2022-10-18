// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/standalone-test/standalone.h>
#include <lib/zbitl/items/debugdata.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <zircon/boot/image.h>

#include <array>
#include <optional>
#include <vector>

#include <zxtest/zxtest.h>

#include "debugdata-info.h"

namespace {

template <typename ZbiIter>
bool IsDebugDataItemAt(ZbiIter it, size_t index) {
  auto expected_debug_data = kDebugdataItems[index];

  auto [header, payload] = *it;
  // Look fot the first item in the run.
  if (header->length != expected_debug_data.aligned_size() + sizeof(zbi_debugdata_t)) {
    return false;
  }
  std::vector<cpp17::byte> buffer(header->length, static_cast<std::byte>(0));
  auto copy_res = it.view().CopyRawItem(cpp20::span(buffer), it);
  EXPECT_TRUE(copy_res.is_ok(), "Copy Error: %*s\n",
              static_cast<int>(copy_res.error_value().zbi_error.size()),
              copy_res.error_value().zbi_error.data());
  if (copy_res.is_error()) {
    return false;
  }

  zbitl::Debugdata item;
  auto init_res = item.Init(buffer);
  EXPECT_TRUE(init_res.is_ok());
  if (init_res.is_error()) {
    return false;
  }
  return item.sink_name() == expected_debug_data.sink &&
         item.vmo_name() == expected_debug_data.vmo_name && item.log() == expected_debug_data.log &&
         item.contents().size() == expected_debug_data.payload.size() &&
         memcmp(item.contents().data(), expected_debug_data.payload.data(),
                item.contents().size()) == 0;
}

TEST(DebugDataPlumbingTest, DebugDataIsPreserved) {
  auto zbi = standalone::GetVmo("zbi");
  zbitl::View view(zbi->borrow());
  size_t matching_items = 0;
  size_t curr_debug_data_item = 0;
  for (auto it = view.begin(); it != view.end(); it++) {
    if (it->header->type != ZBI_TYPE_DEBUGDATA) {
      continue;
    }

    if (IsDebugDataItemAt(it, curr_debug_data_item)) {
      curr_debug_data_item++;
      matching_items++;
    } else {
      curr_debug_data_item = 0;
    }
  }
  view.ignore_error();
  ASSERT_EQ(matching_items, kDebugdataItems.size());
  ASSERT_EQ(curr_debug_data_item, kDebugdataItems.size());
}

}  // namespace
