// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/applet.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/connectivity/weave/lib/applets_loader/testing/applets_loader_test_base.h"

namespace weavestack::applets {
namespace {

using nl::Weave::Profiles::DataManagement_Current::PropertyPathHandle;
using nl::Weave::Profiles::DataManagement_Current::ResourceIdentifier;
using nl::Weave::Profiles::DataManagement_Current::TraitDataSink;
using nl::Weave::Profiles::DataManagement_Current::TraitDataSource;

using applet_test_context = struct {
  size_t source_trait_count;
  size_t sink_trait_count;
};

applet_test_context g_context;

class AppletTest : public testing::AppletsLoaderTestBase {
 protected:
  AppletTest() { AppletTest::ResetContext(); }

  static WEAVE_ERROR PublishTrait(const ResourceIdentifier res_id, const uint64_t instance_id,
                                  TraitDataSource* source_trait) {
    EXPECT_TRUE(source_trait);
    g_context.source_trait_count++;
    return WEAVE_NO_ERROR;
  }

  static WEAVE_ERROR SubscribeTrait(const ResourceIdentifier res_id, const uint64_t instance_id,
                                    PropertyPathHandle base_path_handle,
                                    TraitDataSink* sink_trait) {
    EXPECT_TRUE(sink_trait);
    g_context.sink_trait_count++;
    return WEAVE_NO_ERROR;
  }

  static void ResetContext() {
    g_context = {
        .source_trait_count = 0,
        .sink_trait_count = 0,
    };
  }

  FuchsiaWeaveAppletsCallbacksV1 callbacks_ = {
      .publish_trait = &AppletTest::PublishTrait,
      .subscribe_trait = &AppletTest::SubscribeTrait,
  };
};

TestAppletSpec GetAppletSpec(TraitDataSink** sink_traits, size_t sink_trait_count,
                             TraitDataSource** source_traits, size_t source_trait_count) {
  return (TestAppletSpec){
      .trait_sources =
          {
              .traits = source_traits,
              .count = source_trait_count,
          },
      .trait_sinks =
          {
              .traits = sink_traits,
              .count = sink_trait_count,
          },
  };
}

TEST_F(AppletTest, MoveApplet) {
  test_applets().SetApplet();

  Applet applet1 = applets_loader()->CreateApplet({});
  ASSERT_TRUE(applet1);

  // New, invalid, applet.
  Applet applet2;
  ASSERT_FALSE(applet2);

  // Move applet1 -> applet2.
  applet2 = std::move(applet1);
  ASSERT_TRUE(applet2);
  ASSERT_FALSE(applet1);

  // Create applet3 via move ctor.
  Applet applet3(std::move(applet2));
  ASSERT_TRUE(applet3);
  ASSERT_FALSE(applet2);
}

TEST_F(AppletTest, PublishAndSubscribeTraits) {
  constexpr size_t kSinkTraitCount = 3;
  constexpr size_t kSourceTraitCount = 5;
  TraitDataSink* sinks[kSinkTraitCount];
  TraitDataSource* sources[kSourceTraitCount];

  test_applets().SetApplet().WithSpec(
      GetAppletSpec(sinks, kSinkTraitCount, sources, kSourceTraitCount));

  Applet applet = applets_loader()->CreateApplet(callbacks_);
  ASSERT_TRUE(applet);
  EXPECT_EQ(g_context.sink_trait_count, kSinkTraitCount);
  EXPECT_EQ(g_context.source_trait_count, kSourceTraitCount);
}

}  // namespace
}  // namespace weavestack::applets
