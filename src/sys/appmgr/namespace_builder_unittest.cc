// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/namespace_builder.h"

#include <fcntl.h>
#include <lib/zx/channel.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {
namespace {

TEST(NamespaceBuilder, SystemData) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  rapidjson::Value system_array(rapidjson::kArrayType);
  system_array.PushBack("deprecated-data", allocator);
  system_array.PushBack("deprecated-data/", allocator);
  system_array.PushBack("deprecated-data/subdir", allocator);
  system_array.PushBack("data", allocator);
  system_array.PushBack("data/", allocator);
  system_array.PushBack("data/subdir", allocator);
  document.AddMember("system", system_array, allocator);
  rapidjson::Value services_array(rapidjson::kArrayType);
  document.AddMember("services", services_array, allocator);
  SandboxMetadata sandbox;

  json::JSONParser parser;
  EXPECT_TRUE(sandbox.Parse(document, &parser));

  fxl::UniqueFD dir(open(".", O_RDONLY));
  NamespaceBuilder builder = NamespaceBuilder(std::move(dir), "test_namespace");
  zx_status_t status = builder.AddSandbox(sandbox, [] { return zx::channel(); });
  EXPECT_EQ(ZX_OK, status);

  fdio_flat_namespace_t* ns = builder.Build();
  EXPECT_EQ(0u, ns->count);

  std::vector<std::string> paths;
  for (size_t i = 0; i < ns->count; ++i)
    paths.push_back(ns->path[i]);

  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/data") == paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/deprecated-data") == paths.end());

  for (size_t i = 0; i < ns->count; ++i)
    zx_handle_close(ns->handle[i]);
}

}  // namespace
}  // namespace component
