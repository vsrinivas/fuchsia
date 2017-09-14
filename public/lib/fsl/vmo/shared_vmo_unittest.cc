// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vmo/shared_vmo.h"

#include <string.h>

#include "lib/fsl/vmo/strings.h"
#include "gtest/gtest.h"

namespace fsl {
namespace {

TEST(SharedVmos, Unmappable) {
  std::string content("hello");
  zx::vmo vmo;
  ASSERT_TRUE(VmoFromString(content, &vmo));
  zx_handle_t vmo_handle = vmo.get();

  auto shared_vmo = fxl::MakeRefCounted<SharedVmo>(std::move(vmo));
  ASSERT_NE(nullptr, shared_vmo.get());
  EXPECT_EQ(vmo_handle, shared_vmo->vmo().get());
  EXPECT_EQ(content.size(), shared_vmo->vmo_size());
  EXPECT_EQ(0u, shared_vmo->map_flags());
  EXPECT_EQ(nullptr, shared_vmo->Map());
}

TEST(SharedVmos, Mapped) {
  std::string content("hello");
  zx::vmo vmo;
  ASSERT_TRUE(VmoFromString(content, &vmo));
  zx_handle_t vmo_handle = vmo.get();

  auto shared_vmo =
      fxl::MakeRefCounted<SharedVmo>(std::move(vmo), ZX_VM_FLAG_PERM_READ);
  ASSERT_NE(nullptr, shared_vmo.get());
  EXPECT_EQ(vmo_handle, shared_vmo->vmo().get());
  EXPECT_EQ(content.size(), shared_vmo->vmo_size());
  EXPECT_EQ(ZX_VM_FLAG_PERM_READ, shared_vmo->map_flags());
  const char* data = static_cast<const char*>(shared_vmo->Map());
  EXPECT_NE(nullptr, data);
  EXPECT_EQ(0, memcmp(content.c_str(), data, content.size()));
  EXPECT_EQ(data, static_cast<const char*>(shared_vmo->Map()));
}

}  // namespace
}  // namespace fsl
