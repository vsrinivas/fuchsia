// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace modular {
namespace testing {
namespace {

const char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";

}  // namespace

TestHarnessFixture::TestHarnessFixture() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher_ptr()->CreateComponent(std::move(launch_info),
                                  test_harness_ctrl_.NewRequest());

  test_harness_ = svc_->Connect<fuchsia::modular::testing::TestHarness>();
}

TestHarnessFixture::~TestHarnessFixture() = default;

std::string TestHarnessFixture::InterceptBaseShell(
    fuchsia::modular::testing::TestHarnessSpec* spec) const {
  auto url = GenerateFakeUrl();
  spec->mutable_basemgr_config()
      ->mutable_base_shell()
      ->mutable_app_config()
      ->set_url(url);

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(url);
  spec->mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  return url;
}

std::string TestHarnessFixture::InterceptSessionShell(
    fuchsia::modular::testing::TestHarnessSpec* spec,
    std::string extra_cmx_contents) const {
  auto url = GenerateFakeUrl();

  // 1. Add session shell to modular config.
  {
    fuchsia::modular::session::SessionShellMapEntry entry;
    entry.mutable_config()->mutable_app_config()->set_url(url);

    spec->mutable_basemgr_config()->mutable_session_shell_map()->push_back(
        std::move(entry));
  }

  // 2. Set up interception for session shell.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  if (!extra_cmx_contents.empty()) {
    FXL_CHECK(fsl::VmoFromString(
        extra_cmx_contents, shell_intercept_spec.mutable_extra_cmx_contents()));
  }
  shell_intercept_spec.set_component_url(url);
  spec->mutable_components_to_intercept()->push_back(
      std::move(shell_intercept_spec));

  return url;
}

std::string TestHarnessFixture::InterceptStoryShell(
    fuchsia::modular::testing::TestHarnessSpec* spec) const {
  auto url = GenerateFakeUrl();
  spec->mutable_basemgr_config()
      ->mutable_story_shell()
      ->mutable_app_config()
      ->set_url(url);

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(url);
  spec->mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  return url;
}

std::string TestHarnessFixture::GenerateFakeUrl() const {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  std::string rand_str = std::to_string(random_number);

  // Since we cannot depend on utlitites outside of the stdlib and SDK, here
  // is a quick way to format a string safely.
  std::string url;
  url = "fuchsia-pkg://example.com/GENERATED_URL_";
  url += rand_str;
  url += "#meta/GENERATED_URL_";
  url += rand_str;
  url += ".cmx";

  return url;
}

}  // namespace testing
}  // namespace modular
