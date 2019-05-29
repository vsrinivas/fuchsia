// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace modular {
namespace testing {
namespace {

const char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";

std::string BuildExtraCmx(TestHarnessBuilder::InterceptOptions options) {
  if (options.sandbox_services.empty())
    return "";

  rapidjson::Document cmx;
  cmx.SetObject();
  rapidjson::Value sandbox;
  sandbox.SetObject();
  rapidjson::Value services;
  services.SetArray();
  for (const auto& service : options.sandbox_services) {
    rapidjson::Value v(service, cmx.GetAllocator());
    services.PushBack(v.Move(), cmx.GetAllocator());
  }
  sandbox.AddMember("services", services.Move(), cmx.GetAllocator());
  cmx.AddMember("sandbox", sandbox.Move(), cmx.GetAllocator());

  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  cmx.Accept(writer);
  return buf.GetString();
}

bool BufferFromString(std::string str, fuchsia::mem::Buffer* buffer) {
  ZX_ASSERT(buffer != nullptr);
  uint64_t num_bytes = str.size();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(num_bytes, 0u, &vmo);
  if (status < 0) {
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(str.data(), 0, num_bytes);
    if (status < 0) {
      return false;
    }
  }

  buffer->vmo = std::move(vmo);
  buffer->size = num_bytes;
  return true;
}

}  // namespace

fuchsia::modular::testing::TestHarnessSpec TestHarnessBuilder::BuildSpec() {
  return std::move(spec_);
}

TestHarnessBuilder::OnNewComponentHandler
TestHarnessBuilder::BuildOnNewComponentHandler() {
  return
      [handlers = std::move(handlers_)](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        auto it = handlers.find(startup_info.launch_info.url);
        if (it == handlers.end()) {
          ZX_ASSERT_MSG(false, "Unexpected component URL: %s",
                        startup_info.launch_info.url.c_str());
        }

        it->second(std::move(startup_info), component.Bind());
      };
}

TestHarnessBuilder& TestHarnessBuilder::InterceptComponent(
    OnNewComponentHandler on_new_component, InterceptOptions options) {
  ZX_ASSERT(on_new_component);
  if (options.url.empty()) {
    options.url = GenerateFakeUrl();
  }

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(options.url);
  auto extra_cmx_contents = BuildExtraCmx(options);
  if (!extra_cmx_contents.empty()) {
    ZX_ASSERT(BufferFromString(extra_cmx_contents,
                               intercept_spec.mutable_extra_cmx_contents()));
  }
  spec_.mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  handlers_.insert(std::make_pair(options.url, std::move(on_new_component)));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptBaseShell(
    OnNewComponentHandler on_new_component, InterceptOptions options) {
  if (options.url.empty()) {
    options.url = GenerateFakeUrl("base_shell");
  }
  auto url = options.url;
  InterceptComponent(std::move(on_new_component), std::move(options));

  spec_.mutable_basemgr_config()
      ->mutable_base_shell()
      ->mutable_app_config()
      ->set_url(url);
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptSessionShell(
    OnNewComponentHandler on_new_component, InterceptOptions options) {
  if (options.url.empty()) {
    options.url = GenerateFakeUrl("session_shell");
  }
  auto url = options.url;
  InterceptComponent(std::move(on_new_component), std::move(options));

  fuchsia::modular::session::SessionShellMapEntry entry;
  entry.mutable_config()->mutable_app_config()->set_url(url);
  spec_.mutable_basemgr_config()->mutable_session_shell_map()->push_back(
      std::move(entry));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptStoryShell(
    OnNewComponentHandler on_new_component, InterceptOptions options) {
  if (options.url.empty()) {
    options.url = GenerateFakeUrl("story_shell");
  }
  auto url = options.url;
  InterceptComponent(std::move(on_new_component), std::move(options));

  spec_.mutable_basemgr_config()
      ->mutable_story_shell()
      ->mutable_app_config()
      ->set_url(url);
  return *this;
}

std::string TestHarnessBuilder::GenerateFakeUrl(std::string name) const {
  name.erase(
      std::remove_if(name.begin(), name.end(),
                     [](auto const& c) -> bool { return !std::isalnum(c); }),
      name.end());

  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  std::string rand_str = std::to_string(random_number);

  // Since we cannot depend on utlitites outside of the stdlib and SDK, here
  // is a quick way to format a string safely.
  std::string url;
  url = "fuchsia-pkg://example.com/GENERATED_URL_";
  url += rand_str;
  url += "#meta/GENERATED_URL_";
  if (!name.empty()) {
    url += name;
    url += "_";
  }
  url += rand_str;
  url += ".cmx";

  return url;
}

TestHarnessFixture::~TestHarnessFixture() = default;

std::string TestHarnessFixture::InterceptBaseShell(
    fuchsia::modular::testing::TestHarnessSpec* spec) const {
  auto url = TestHarnessBuilder().GenerateFakeUrl();
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
  auto url = TestHarnessBuilder().GenerateFakeUrl();

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
    ZX_ASSERT(BufferFromString(
        extra_cmx_contents, shell_intercept_spec.mutable_extra_cmx_contents()));
  }
  shell_intercept_spec.set_component_url(url);
  spec->mutable_components_to_intercept()->push_back(
      std::move(shell_intercept_spec));

  return url;
}

std::string TestHarnessFixture::InterceptStoryShell(
    fuchsia::modular::testing::TestHarnessSpec* spec) const {
  auto url = TestHarnessBuilder().GenerateFakeUrl();
  spec->mutable_basemgr_config()
      ->mutable_story_shell()
      ->mutable_app_config()
      ->set_url(url);

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(url);
  spec->mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  return url;
}

TestHarnessFixture::TestHarnessFixture() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launcher_ptr()->CreateComponent(std::move(launch_info),
                                  test_harness_ctrl_.NewRequest());

  test_harness_ = svc_->Connect<fuchsia::modular::testing::TestHarness>();
}

}  // namespace testing
}  // namespace modular
