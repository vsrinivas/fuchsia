// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/tests/gfx_integration_tests/pixel_test.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

namespace integration_tests {

using RealmRoot = component_testing::RealmRoot;

namespace {

const int64_t kTestTimeout = 90;
constexpr auto kVkCube = "wrapper_vk_cube";
constexpr auto kVkCubeUrl = "#meta/wrapper_vk_cube.cm";

}  // namespace

class VkcubeTest : public PixelTest {
 private:
  RealmRoot SetupRealm() {
    ViewProviderConfig config = {.name = kVkCube, .component_url = kVkCubeUrl};

    RealmBuilderArgs args = {.scene_owner = SceneOwner::ROOT_PRESENTER,
                             .view_provider_config = std::move(config)};

    return ScenicRealmBuilder(std::move(args))
        .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
        .AddRealmProtocol(fuchsia::ui::annotation::Registry::Name_)
        .AddSceneOwnerProtocol(fuchsia::ui::policy::Presenter::Name_)
        .Build();
  }
};

TEST_F(VkcubeTest, ProtectedVkcube) {
  // vkcube-on-scenic does not produce protected content if platform does not allow. Check if
  // protected memory is available beforehand to skip these cases.
  {
    if (!scenic_impl::gfx::test::VkSessionTest::CreateVulkanDeviceQueues(
            /*use_protected_memory=*/true)) {
      GTEST_SKIP();
    }
  }

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  auto view_provider = realm()->Connect<fuchsia::ui::app::ViewProvider>();

  view_provider->CreateViewWithViewRef(std::move(view_token.value), std::move(view_ref_control),
                                       std::move(view_ref));

  std::optional<bool> view_state_changed_observed;
  EmbedderView embedder_view(CreatePresentationContext(), std::move(view_holder_token));

  embedder_view.EmbedView(
      [&view_state_changed_observed](auto) { view_state_changed_observed = true; });

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&view_state_changed_observed] { return view_state_changed_observed.has_value(); },
      zx::sec(kTestTimeout)));
}

}  // namespace integration_tests
