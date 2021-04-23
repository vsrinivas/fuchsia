// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

TEST(VulkanLoader, SystemLoad) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  zx::vmo vmo_out;
  // The test instance reads from /pkg/bin, and this executable is guaranteed to be there.
  EXPECT_EQ(ZX_OK, loader->Get("pkg-server", &vmo_out));
  EXPECT_TRUE(vmo_out.is_valid());
  zx_info_handle_basic_t handle_info;
  EXPECT_EQ(ZX_OK, vmo_out.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info),
                                    nullptr, nullptr));
  EXPECT_TRUE(handle_info.rights & ZX_RIGHT_EXECUTE);
  EXPECT_EQ(ZX_OK, loader->Get("not-present", &vmo_out));
  EXPECT_FALSE(vmo_out.is_valid());
}

TEST(VulkanLoader, ManifestLoad) {
  fuchsia::vulkan::loader::LoaderSyncPtr loader;
  EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.vulkan.loader.Loader",
                                        loader.NewRequest().TakeChannel().release()));

  zx::vmo vmo_out;
  // manifest.json remaps this to bin/pkg-server.
  EXPECT_EQ(ZX_OK, loader->Get("pkg-server2", &vmo_out));
  EXPECT_TRUE(vmo_out.is_valid());
  zx_info_handle_basic_t handle_info;
  EXPECT_EQ(ZX_OK, vmo_out.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info),
                                    nullptr, nullptr));
  EXPECT_TRUE(handle_info.rights & ZX_RIGHT_EXECUTE);
  EXPECT_EQ(ZX_OK, loader->Get("not-present", &vmo_out));
  EXPECT_FALSE(vmo_out.is_valid());
}
