// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/namespace_builder.h"

#include <fcntl.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls/object.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fuchsia/sys/cpp/fidl.h"
#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"
#include "zircon/types.h"

namespace component {
namespace {

TEST(NamespaceBuilder, SystemData) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  rapidjson::Value system_array(rapidjson::kArrayType);
  system_array.PushBack("deprecated-data", allocator);
  system_array.PushBack("deprecated-data/subdir", allocator);
  system_array.PushBack("data", allocator);
  system_array.PushBack("data/subdir", allocator);
  document.AddMember("system", system_array, allocator);
  rapidjson::Value services_array(rapidjson::kArrayType);
  document.AddMember("services", services_array, allocator);
  SandboxMetadata sandbox;

  json::JSONParser parser;
  sandbox.Parse(document, &parser);
  EXPECT_FALSE(parser.HasError()) << parser.error_str();

  fbl::unique_fd dir(open(".", O_RDONLY));
  NamespaceBuilder builder = NamespaceBuilder(std::move(dir), "test_namespace");
  zx_status_t status = builder.AddSandbox(
      sandbox, []() -> fidl::InterfaceHandle<fuchsia::io::Directory> { return {}; });
  EXPECT_EQ(ZX_OK, status);

  fdio_flat_namespace_t* ns = builder.Build();
  EXPECT_EQ(0u, ns->count);

  std::vector<std::string> paths;
  for (size_t i = 0; i < ns->count; ++i)
    paths.push_back(ns->path[i]);

  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/data") == paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/deprecated-data") == paths.end());

  for (size_t i = 0; i < ns->count; ++i) {
    zx_handle_close(ns->handle[i]);
  }
}

zx_koid_t GetRelatedKoid(const zx::unowned_channel& object) {
  zx_info_handle_basic info = {};
  zx_status_t status =
      object->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to get object info: %s", zx_status_get_string(status));
  return info.related_koid;
}

zx_koid_t GetKoid(const zx::unowned_channel& object) {
  zx_info_handle_basic info = {};
  zx_status_t status =
      object->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to get object info: %s", zx_status_get_string(status));
  return info.koid;
}

TEST(NamespaceBuilder, FlatNamespaceAreAddedToComponent) {
  fbl::unique_fd dir(open(".", O_RDONLY));
  NamespaceBuilder builder = NamespaceBuilder(std::move(dir), "test_namespace");
  auto flat_namespace = fuchsia::sys::FlatNamespace::New();
  fidl::InterfaceHandle<fuchsia::io::Directory> client;
  fidl::InterfaceRequest server = client.NewRequest();
  // The related Koid should equal the Koid of the opposite end of a channel.
  // So the related Koid of the client end should equal the Koid of the server
  // end here, and vice versa.
  ASSERT_EQ(GetRelatedKoid(client.channel().borrow()), GetKoid(server.channel().borrow()));
  flat_namespace->directories.push_back(std::move(client));
  flat_namespace->paths.push_back("/dev/class/usb-device");
  builder.AddFlatNamespace(std::move(flat_namespace));

  fdio_flat_namespace_t* ns = builder.Build();
  EXPECT_EQ(1u, ns->count);

  std::vector<std::string> paths;
  EXPECT_EQ(std::string(ns->path[0]), std::string("/dev/class/usb-device"));
  // Assert that the channel in |ns| is connected to the server end.
  EXPECT_EQ(GetRelatedKoid(zx::unowned_channel(ns->handle[0])), GetKoid(server.channel().borrow()));

  for (size_t i = 0; i < ns->count; ++i) {
    zx_handle_close(ns->handle[i]);
  }
}

}  // namespace
}  // namespace component
