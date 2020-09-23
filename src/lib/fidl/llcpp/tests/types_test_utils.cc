// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "types_test_utils.h"

namespace llcpp_types_test_utils {

void HandleChecker::AddEvent(const zx_handle_t event) {
  zx_handle_t dupe;
  ASSERT_EQ(zx_handle_duplicate(event, ZX_RIGHT_SAME_RIGHTS, &dupe), ZX_OK);
  events_.emplace_back(zx::event(dupe));
}

void HandleChecker::CheckEvents() {
  for (size_t i = 0; i < events_.size(); ++i) {
    zx_info_handle_count_t info = {};
    auto status = events_[i].get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
    ZX_ASSERT(status == ZX_OK);
    EXPECT_EQ(info.handle_count, 1U) << "Handle not freed " << (i + 1) << '/' << events_.size();
  }
}

}  // namespace llcpp_types_test_utils
