// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/namespace.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <gtest/gtest.h>

#include "src/devices/lib/driver2/test_base.h"

namespace fio = fuchsia::io;
namespace frunner = fuchsia_component_runner;

TEST(NamespaceTest, CreateAndConnect) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  auto pkg = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_EQ(ZX_OK, pkg.status_value());
  fidl::FidlAllocator allocator;
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(allocator, 1);
  ns_entries[0].Allocate(allocator);
  ns_entries[0].set_path(allocator, "/pkg").set_directory(allocator, std::move(pkg->client));
  auto ns = driver::Namespace::Create(ns_entries);
  ASSERT_TRUE(ns.is_ok());

  driver::testing::Directory pkg_directory;
  fidl::Binding<fio::Directory> pkg_binding(&pkg_directory);
  pkg_binding.Bind(pkg->server.TakeChannel(), loop.dispatcher());

  zx::channel server_end;
  pkg_directory.SetOpenHandler([&server_end](std::string path, auto object) {
    EXPECT_EQ("path-exists", path);
    server_end = std::move(object.TakeChannel());
  });
  auto client_end = ns->Connect<frunner::ComponentRunner>("/pkg/path-exists");
  EXPECT_TRUE(client_end.is_ok());
  loop.RunUntilIdle();
  zx_info_handle_basic_t client_info = {}, server_info = {};
  ASSERT_EQ(ZX_OK, client_end->channel().get_info(ZX_INFO_HANDLE_BASIC, &client_info,
                                                  sizeof(client_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, server_end.get_info(ZX_INFO_HANDLE_BASIC, &server_info, sizeof(server_info),
                                       nullptr, nullptr));
  EXPECT_EQ(client_info.koid, server_info.related_koid);

  auto not_found_client_end = ns->Connect<frunner::ComponentRunner>("/svc/path-does-not-exist");
  EXPECT_EQ(ZX_ERR_NOT_FOUND, not_found_client_end.error_value());
}

TEST(NamespaceTest, CreateFailed) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  fidl::FidlAllocator allocator;
  fidl::VectorView<frunner::wire::ComponentNamespaceEntry> ns_entries(allocator, 1);
  ns_entries[0].Allocate(allocator);
  ns_entries[0].set_path(allocator, "/pkg").set_directory(allocator);
  auto ns = driver::Namespace::Create(ns_entries);
  ASSERT_TRUE(ns.is_error());
}
