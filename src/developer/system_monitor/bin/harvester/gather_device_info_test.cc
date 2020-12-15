// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_device_info.h"

#include <gtest/gtest.h>

#include "build_info.h"
#include "dockyard_proxy_fake.h"
#include "root_resource.h"

class GatherDeviceInfoTest : public ::testing::Test {};

class FakeAnnotationsProvider : public harvester::AnnotationsProvider {
 public:
  FakeAnnotationsProvider() = default;
  ~FakeAnnotationsProvider() override = default;

  void SetAnnotation(std::string key, std::string value) {
    fake_annotations_[key] = value;
  }

  harvester::BuildAnnotations GetAnnotations() override {
    harvester::BuildAnnotations result = harvester::BuildAnnotations{
        .buildBoard =
            harvester::BuildInfoValue(harvester::BuildInfoError::kMissingValue),
        .buildProduct =
            harvester::BuildInfoValue(harvester::BuildInfoError::kMissingValue),
        .deviceBoardName =
            harvester::BuildInfoValue(harvester::BuildInfoError::kMissingValue),
    };

    for (auto &[key, value] : fake_annotations_) {
      if (key == "build.board") {
        result.buildBoard = harvester::BuildInfoValue(value);
      } else if (key == "build.product") {
        result.buildProduct = harvester::BuildInfoValue(value);
      } else if (key == "device.board-name") {
        result.deviceBoardName = harvester::BuildInfoValue(value);
      }
    }
    return result;
  }

 private:
  std::map<std::string, std::string> fake_annotations_;
};

TEST_F(GatherDeviceInfoTest, WithAllExpectedValues) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  std::unique_ptr<FakeAnnotationsProvider> annotations_provider =
      std::make_unique<FakeAnnotationsProvider>();

  annotations_provider->SetAnnotation("build.board", "x86-test");
  annotations_provider->SetAnnotation("build.product", "some-product");
  annotations_provider->SetAnnotation("device.board-name", "device-board-name");

  harvester::GatherDeviceInfo gatherer(root_resource, &dockyard_proxy,
                                       std::move(annotations_provider));

  gatherer.GatherDeviceProperties();
  std::string test_value;

  EXPECT_TRUE(dockyard_proxy.CheckStringSent(harvester::kAnnotationBuildBoard,
                                             &test_value));
  EXPECT_EQ("x86-test", test_value);
  EXPECT_TRUE(dockyard_proxy.CheckStringSent(harvester::kAnnotationBuildProduct,
                                             &test_value));
  EXPECT_EQ("some-product", test_value);
  EXPECT_TRUE(dockyard_proxy.CheckStringSent(
      harvester::kAnnotationDeviceBoardName, &test_value));
  EXPECT_EQ("device-board-name", test_value);
}

TEST_F(GatherDeviceInfoTest, HandlesSomeMissingExpectedValues) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  std::unique_ptr<FakeAnnotationsProvider> annotations_provider =
      std::make_unique<FakeAnnotationsProvider>();

  annotations_provider->SetAnnotation("build.board", "x86-test");
  annotations_provider->SetAnnotation("device.NOT-board-name",
                                      "NOT-device-board-name");

  harvester::GatherDeviceInfo gatherer(root_resource, &dockyard_proxy,
                                       std::move(annotations_provider));

  gatherer.GatherDeviceProperties();
  std::string test_value;

  EXPECT_TRUE(dockyard_proxy.CheckStringSent(harvester::kAnnotationBuildBoard,
                                             &test_value));
  EXPECT_EQ("x86-test", test_value);
  EXPECT_FALSE(dockyard_proxy.CheckStringSent(
      harvester::kAnnotationBuildProduct, &test_value));
  EXPECT_FALSE(dockyard_proxy.CheckStringSent(
      harvester::kAnnotationDeviceBoardName, &test_value));
}

TEST_F(GatherDeviceInfoTest, HandlesAllMissingExpectedValues) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  std::unique_ptr<FakeAnnotationsProvider> annotations_provider =
      std::make_unique<FakeAnnotationsProvider>();

  harvester::GatherDeviceInfo gatherer(root_resource, &dockyard_proxy,
                                       std::move(annotations_provider));

  gatherer.GatherDeviceProperties();
  std::string test_value;

  EXPECT_FALSE(dockyard_proxy.CheckStringSent(harvester::kAnnotationBuildBoard,
                                              &test_value));
  EXPECT_FALSE(dockyard_proxy.CheckStringSent(
      harvester::kAnnotationBuildProduct, &test_value));
  EXPECT_FALSE(dockyard_proxy.CheckStringSent(
      harvester::kAnnotationDeviceBoardName, &test_value));
}

TEST_F(GatherDeviceInfoTest, GatherGetsUptime) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  std::unique_ptr<FakeAnnotationsProvider> annotations_provider =
      std::make_unique<FakeAnnotationsProvider>();

  harvester::GatherDeviceInfo gatherer(root_resource, &dockyard_proxy,
                                       std::move(annotations_provider));

  gatherer.Gather();
  uint64_t test_value;

  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent(harvester::kAnnotationUptime, &test_value));
  EXPECT_LT(1ULL, test_value);
}
