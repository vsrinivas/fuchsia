// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/mapped-vmo-file.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

constexpr std::string_view kContents = "file contents";

TEST(ElfldltlMappedVmoFileTests, Basic) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kContents.size(), 0, &vmo));
  ASSERT_OK(vmo.write(kContents.data(), 0, kContents.size()));

  elfldltl::MappedVmoFile vmofile;
  ASSERT_TRUE(vmofile.Init(vmo.borrow()).is_ok());

  // The VMO handle is not used by reading below, so it can be closed.
  vmo.reset();

  {
    // Test move-construction and move-assignment.
    elfldltl::MappedVmoFile moved_vmofile(std::move(vmofile));
    vmofile = std::move(moved_vmofile);
  }

  auto res =
      vmofile.ReadArrayFromFile<char>(0, elfldltl::NoArrayFromFile<char>(), kContents.size());
  ASSERT_TRUE(res);
  std::string_view sv{res->data(), res->size()};
  EXPECT_EQ(sv, kContents);

  // Test that moving then destroying works.
  elfldltl::MappedVmoFile moved_vmofile(std::move(vmofile));
}

TEST(ElfldltlMappedVmoFileTests, BadVmo) {
  elfldltl::MappedVmoFile vmofile;
  auto result = vmofile.Init(zx::unowned_vmo());
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_BAD_HANDLE);
}

}  // namespace
