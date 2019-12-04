// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <gtest/gtest.h>
#include <src/lib/files/file.h>

#include "src/lib/files/directory.h"

namespace camera {
namespace {

constexpr const char* kDirPath = "/data/calibration";
constexpr uint8_t kStrLength = 17;

TEST(FactoryProtocolTest, ConstructorSanity) {
  auto factory_impl = FactoryProtocol::Create();
  ASSERT_NE(nullptr, factory_impl);
}

TEST(FactoryProtocolTest, StreamingWritesToFile) {
  auto factory_impl = FactoryProtocol::Create();
  ASSERT_NE(nullptr, factory_impl);
  ASSERT_EQ(ZX_OK, factory_impl->ConnectToStream());
  const std::string kDirPathStr(kDirPath, kStrLength);
  ASSERT_TRUE(files::IsDirectory(kDirPathStr));
}

}  // namespace
}  // namespace camera
