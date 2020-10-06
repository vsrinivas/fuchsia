// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular/testing/cpp/test_harness_builder.h>

#include <sstream>

namespace modular_testing {
namespace {

std::string StringsToCSV(const std::vector<std::string>& strings) {
  std::stringstream csv;
  for (size_t i = 0; i < strings.size(); i++) {
    if (i != 0) {
      csv << ",";
    }
    csv << "\"" << strings[i] << "\"";
  }
  return csv.str();
}

std::string BuildExtraCmx(const TestHarnessBuilder::InterceptOptions& options) {
  std::stringstream ss;
  ss << R"({
    "sandbox": {
      "services": [
        )";
  ss << StringsToCSV(options.sandbox_services);
  ss << R"(
      ]
    }
  })";
  return ss.str();
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

TestHarnessBuilder::TestHarnessBuilder(fuchsia::modular::testing::TestHarnessSpec spec)
    : spec_(std::move(spec)), env_services_(new vfs::PseudoDir) {}

TestHarnessBuilder::TestHarnessBuilder()
    : TestHarnessBuilder(fuchsia::modular::testing::TestHarnessSpec()) {}

fuchsia::modular::testing::TestHarnessSpec TestHarnessBuilder::BuildSpec() {
  fuchsia::io::DirectoryPtr dir;
  // This directory must be READABLE *and* WRITABLE, otherwise service
  // connections fail.
  env_services_->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                       dir.NewRequest().TakeChannel());
  spec_.mutable_env_services()->set_service_dir(dir.Unbind().TakeChannel());
  return std::move(spec_);
}

TestHarnessBuilder::LaunchHandler TestHarnessBuilder::BuildOnNewComponentHandler() {
  return [handlers = std::move(handlers_)](
             fuchsia::sys::StartupInfo startup_info,
             fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
    auto it = handlers.find(startup_info.launch_info.url);
    if (it == handlers.end()) {
      ZX_ASSERT_MSG(false, "Unexpected component URL: %s", startup_info.launch_info.url.c_str());
    }

    it->second(std::move(startup_info), component.Bind());
  };
}

void TestHarnessBuilder::BuildAndRun(
    const fuchsia::modular::testing::TestHarnessPtr& test_harness) {
  test_harness.events().OnNewComponent = BuildOnNewComponentHandler();
  test_harness->Run(BuildSpec());
}

TestHarnessBuilder& TestHarnessBuilder::InterceptComponent(InterceptOptions options) {
  ZX_ASSERT(options.launch_handler);
  ZX_ASSERT(!options.url.empty());

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(options.url);
  auto extra_cmx_contents = BuildExtraCmx(options);
  if (!extra_cmx_contents.empty()) {
    ZX_ASSERT(BufferFromString(extra_cmx_contents, intercept_spec.mutable_extra_cmx_contents()));
  }
  spec_.mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  handlers_.insert(std::make_pair(options.url, std::move(options.launch_handler)));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptSessionShell(InterceptOptions options) {
  fuchsia::modular::session::SessionShellMapEntry entry;
  entry.mutable_config()->mutable_app_config()->set_url(options.url);
  spec_.mutable_basemgr_config()->mutable_session_shell_map()->push_back(std::move(entry));
  InterceptComponent(std::move(options));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptStoryShell(InterceptOptions options) {
  spec_.mutable_basemgr_config()->mutable_story_shell()->mutable_app_config()->set_url(options.url);
  InterceptComponent(std::move(options));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::InterceptSessionLauncherComponent(
    InterceptOptions options, fit::optional<std::vector<std::string>> args) {
  spec_.mutable_basemgr_config()->mutable_session_launcher()->set_url(options.url);
  if (args.has_value()) {
    spec_.mutable_basemgr_config()->mutable_session_launcher()->set_args(std::move(args.value()));
  }
  InterceptComponent(std::move(options));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::AddService(const std::string& service_name,
                                                   vfs::Service::Connector connector) {
  env_services_->AddEntry(service_name, std::make_unique<vfs::Service>(std::move(connector)));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::AddServiceFromComponent(const std::string& service_name,
                                                                const std::string& component_url) {
  fuchsia::modular::testing::ComponentService svc;
  svc.name = service_name;
  svc.url = component_url;
  spec_.mutable_env_services()->mutable_services_from_components()->push_back(std::move(svc));
  return *this;
}

TestHarnessBuilder& TestHarnessBuilder::AddServiceFromServiceDirectory(
    const std::string& service_name, std::shared_ptr<sys::ServiceDirectory> services) {
  return AddService(service_name, [service_name, services](zx::channel request,
                                                           async_dispatcher_t* dispatcher) mutable {
    services->Connect(service_name, std::move(request));
  });
}

TestHarnessBuilder& TestHarnessBuilder::UseSessionShellForStoryShellFactory() {
  spec_.mutable_basemgr_config()->set_use_session_shell_for_story_shell_factory(true);
  return *this;
}

// static
std::string TestHarnessBuilder::GenerateFakeUrl(std::string name) {
  name.erase(std::remove_if(name.begin(), name.end(),
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

}  // namespace modular_testing
