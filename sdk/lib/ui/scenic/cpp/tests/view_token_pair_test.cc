// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/views/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <tuple>

namespace {

TEST(ViewTokenPairTest, ViewTokensAreRelated) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  // Get info about the |view_token|.
  zx_info_handle_basic_t view_info;
  zx_status_t view_status =
      zx_object_get_info(view_token.value.get(), ZX_INFO_HANDLE_BASIC,
                         &view_info, sizeof(view_info), nullptr, nullptr);
  EXPECT_EQ(view_status, ZX_OK);

  // Get info about the |view_holder_token|.
  zx_info_handle_basic_t view_holder_info;
  zx_status_t view_holder_status = zx_object_get_info(
      view_holder_token.value.get(), ZX_INFO_HANDLE_BASIC, &view_holder_info,
      sizeof(view_holder_info), nullptr, nullptr);
  EXPECT_EQ(view_holder_status, ZX_OK);

  // The tokens' koids should always be related.
  EXPECT_EQ(view_info.koid, view_holder_info.related_koid);
  EXPECT_EQ(view_info.related_koid, view_holder_info.koid);
}

}  // namespace
