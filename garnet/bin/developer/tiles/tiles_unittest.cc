// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/developer/tiles/tiles.h"

#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl_test_base.h>
#include <gtest/gtest.h>
#include <lib/fxl/logging.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace {

class FakeViewManager
    : public fuchsia::ui::viewsv1::testing::ViewManager_TestBase {
 public:
  FakeViewManager() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewManager> req) {
    binding_.Bind(std::move(req));
  }

  void NotImplemented_(const std::string& name) {
    // Do nothing.
  }

  fidl::Binding<fuchsia::ui::viewsv1::ViewManager> binding_;
};

class TilesTest : public gtest::TestLoopFixture {
 public:
  TilesTest() {}

  void SetUp() final {
    auto tokens = scenic::NewViewTokenPair();
    view_holder_token_ = std::move(tokens.second);

    tiles_impl_ = std::make_unique<tiles::Tiles>(
        context_provider_.context(), std::move(tokens.first),
        std::vector<std::string>(), 10);
    tiles_ = tiles_impl_.get();
  }

  void TearDown() final {
    tiles_ = nullptr;
    tiles_impl_.reset();
  }

  fuchsia::developer::tiles::Controller* tiles() const { return tiles_; }

 private:
  FakeViewManager view_manager_;
  fuchsia::ui::views::ViewHolderToken view_holder_token_;
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<tiles::Tiles> tiles_impl_;
  fuchsia::developer::tiles::Controller* tiles_;
};

TEST_F(TilesTest, Trivial) {}

TEST_F(TilesTest, AddFromURL) {
  uint32_t key = 0;
  tiles()->AddTileFromURL("test_tile", /* allow_focus */ true, {},
                          [&key](uint32_t cb_key) {
                            EXPECT_NE(0u, cb_key) << "Key should be nonzero";
                            key = cb_key;
                          });

  ASSERT_NE(0u, key) << "Key should be nonzero";

  tiles()->ListTiles([key](::std::vector<uint32_t> keys,
                           ::std::vector<::std::string> urls,
                           ::std::vector<::fuchsia::math::SizeF> sizes,
                           ::std::vector<bool> focusabilities) {
    ASSERT_EQ(1u, keys.size());
    EXPECT_EQ(1u, urls.size());
    EXPECT_EQ(1u, sizes.size());
    EXPECT_EQ(1u, focusabilities.size());
    EXPECT_EQ(key, keys.at(0));
    EXPECT_EQ("test_tile", urls.at(0));
    EXPECT_EQ(true, focusabilities.at(0));
  });

  tiles()->RemoveTile(key);

  tiles()->ListTiles([](::std::vector<uint32_t> keys,
                        ::std::vector<::std::string> urls,
                        ::std::vector<::fuchsia::math::SizeF> sizes,
                        ::std::vector<bool> focusabilities) {
    EXPECT_EQ(0u, keys.size());
    EXPECT_EQ(0u, urls.size());
    EXPECT_EQ(0u, sizes.size());
    EXPECT_EQ(0u, focusabilities.size());
  });

  tiles()->AddTileFromURL("test_nofocus_tile", /* allow_focus */ false, {},
                          [&key](uint32_t _cb_key) {
                            // noop
                          });
  tiles()->ListTiles([](::std::vector<uint32_t> keys,
                        ::std::vector<::std::string> urls,
                        ::std::vector<::fuchsia::math::SizeF> sizes,
                        ::std::vector<bool> focusabilities) {
    EXPECT_EQ(1u, keys.size());
    EXPECT_EQ(1u, urls.size());
    EXPECT_EQ(1u, sizes.size());
    EXPECT_EQ(1u, focusabilities.size());
    EXPECT_EQ(false, focusabilities.at(0));
  });
}

}  // anonymous namespace
