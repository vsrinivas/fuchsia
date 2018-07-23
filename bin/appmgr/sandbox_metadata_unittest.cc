// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/sandbox_metadata.h"

#include "gtest/gtest.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

TEST(SandboxMetadata, Parse) {
  rapidjson::Document dev_document;
  dev_document.SetObject();
  rapidjson::Document::AllocatorType& dev_allocator =
      dev_document.GetAllocator();
  rapidjson::Value dev_array(rapidjson::kArrayType);
  dev_array.PushBack("class/input", dev_allocator);
  dev_document.AddMember("dev", dev_array, dev_allocator);
  SandboxMetadata dev_sandbox;
  EXPECT_TRUE(dev_sandbox.Parse(dev_document));
  EXPECT_EQ(1u, dev_sandbox.dev().size());
  EXPECT_EQ(0u, dev_sandbox.features().size());
  EXPECT_EQ("class/input", dev_sandbox.dev()[0]);

  rapidjson::Document feat_document;
  feat_document.SetObject();
  rapidjson::Document::AllocatorType& feat_allocator =
      feat_document.GetAllocator();
  rapidjson::Value feat_array(rapidjson::kArrayType);
  feat_array.PushBack("vulkan", feat_allocator);
  feat_document.AddMember("features", feat_array, feat_allocator);
  SandboxMetadata feat_sandbox;
  EXPECT_TRUE(feat_sandbox.Parse(feat_document));
  EXPECT_EQ(0u, feat_sandbox.dev().size());
  EXPECT_EQ(1u, feat_sandbox.features().size());
  EXPECT_EQ("vulkan", feat_sandbox.features()[0]);
  EXPECT_TRUE(feat_sandbox.HasFeature("vulkan"));
  EXPECT_FALSE(feat_sandbox.HasFeature("banana"));
}

}  // namespace
}  // namespace component
