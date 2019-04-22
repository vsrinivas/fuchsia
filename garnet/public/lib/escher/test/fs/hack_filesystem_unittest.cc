// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/fs/hack_filesystem.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

TEST(HackFilesystem, Init) {
  auto fs = HackFilesystem::New();
  bool success =
      fs->InitializeWithRealFiles({"shaders/model_renderer/main.vert"});

  EXPECT_TRUE(success);
  HackFileContents contents = fs->ReadFile("shaders/model_renderer/main.vert");
  EXPECT_GT(contents.size(), 0U);
  EXPECT_EQ(contents.substr(0, 12), "#version 450");
}

}  // namespace
