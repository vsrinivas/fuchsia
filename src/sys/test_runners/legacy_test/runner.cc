// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runner.h"

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/runner/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

#include <garnet/bin/run_test_component/test_metadata.h>
#include <src/lib/fsl/io/fd.h>
#include <src/lib/fsl/vmo/file.h>
#include <src/lib/fsl/vmo/sized_vmo.h>
#include <src/lib/pkg_url/fuchsia_pkg_url.h>

#include "test_component.h"

namespace {
struct ComponentArgs {
  std::string legacy_url;
  std::shared_ptr<run::TestMetadata> test_metadata;
  std::shared_ptr<sys::ServiceDirectory> test_component_svc;
  fidl::InterfaceHandle<fuchsia::io::Directory> component_pkg;
  std::vector<fuchsia::component::runner::ComponentNamespaceEntry> ns;
};

fit::result<ComponentArgs, fuchsia::component::Error> GetComponentArgs(
    fuchsia::component::runner::ComponentStartInfo& start_info) {
  component::FuchsiaPkgUrl url;
  if (!url.Parse(start_info.resolved_url())) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ", as we cannot parse url.";
    return fit::error(fuchsia::component::Error::INVALID_ARGUMENTS);
  }

  if (!start_info.program().has_entries()) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ", as it has no program entry.";
    return fit::error(fuchsia::component::Error::INVALID_ARGUMENTS);
  }
  auto& program_entries = start_info.program().entries();
  auto it = std::find_if(
      program_entries.begin(), program_entries.end(),
      [](const fuchsia::data::DictionaryEntry& entry) { return entry.key == "legacy_manifest"; });
  if (it == program_entries.end()) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ", as it has no legacy_manifest entry.";
    return fit::error(fuchsia::component::Error::INVALID_ARGUMENTS);
  }

  auto ns = std::move(*start_info.mutable_ns());
  const std::string& legacy_manifest = it->value->str();

  auto pkg_it = std::find_if(ns.begin(), ns.end(),
                             [](fuchsia::component::runner::ComponentNamespaceEntry& entry) {
                               return entry.path() == "/pkg";
                             });

  auto component_pkg = std::move(*pkg_it->mutable_directory());

  auto fd = fsl::OpenChannelAsFileDescriptor(component_pkg.TakeChannel());
  fsl::SizedVmo vmo;
  if (!fsl::VmoFromFilenameAt(fd.get(), legacy_manifest, &vmo)) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ", as cannot read legacy manifest file.";
    return fit::error(fuchsia::component::Error::INSTANCE_CANNOT_START);
  }

  const uint64_t size = vmo.size();
  std::string cmx_str(size, ' ');
  auto status = vmo.vmo().read(cmx_str.data(), 0, size);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ", as cannot read legacy manifest file: " << zx_status_get_string(status)
                     << ".";
    return fit::error(fuchsia::component::Error::INSTANCE_CANNOT_START);
  }
  auto legacy_url = url.package_path() + "#" + legacy_manifest;
  auto test_metadata = std::make_shared<run::TestMetadata>();
  if (!test_metadata->ParseFromString(cmx_str, legacy_manifest)) {
    FX_LOGS(WARNING) << "cannot run test: " << start_info.resolved_url()
                     << ".\nError parsing cmx: " << legacy_manifest << ", "
                     << test_metadata->error_str();
    return fit::error(fuchsia::component::Error::INSTANCE_CANNOT_START);
  }

  auto svc_it = std::find_if(ns.begin(), ns.end(),
                             [](fuchsia::component::runner::ComponentNamespaceEntry& entry) {
                               return entry.path() == "/svc";
                             });

  auto component_svc =
      std::make_shared<sys::ServiceDirectory>(std::move(*svc_it->mutable_directory()));

  return fit::ok(ComponentArgs{.legacy_url = std::move(legacy_url),
                               .test_metadata = std::move(test_metadata),
                               .test_component_svc = std::move(component_svc),
                               .component_pkg = std::move(component_pkg),
                               .ns = std::move(ns)});
}

}  // namespace

Runner::Runner(std::shared_ptr<sys::ServiceDirectory> svc, async_dispatcher_t* dispatcher)
    : svc_(std::move(svc)), dispatcher_(dispatcher) {}

Runner::~Runner() = default;

void Runner::Start(
    fuchsia::component::runner::ComponentStartInfo start_info,
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) {
  auto args_result = GetComponentArgs(start_info);

  if (args_result.is_error()) {
    controller.Close(static_cast<zx_status_t>(args_result.take_error()));
    return;
  }

  auto args = args_result.take_value();
  FX_LOGS(INFO) << "running test: " << args.legacy_url;

  auto env_proxy = svc_->Connect<fuchsia::sys::Environment>();
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  env_proxy->GetDirectory(dir.NewRequest().TakeChannel());
  auto env_svc = std::make_shared<sys::ServiceDirectory>(dir.TakeChannel());

  auto test_component = std::make_unique<TestComponent>(
      TestComponentArgs{.legacy_url = std::move(args.legacy_url),
                        .outgoing_dir = start_info.mutable_outgoing_dir()->TakeChannel(),
                        .parent_env = std::move(env_proxy),
                        .parent_env_svc = std::move(env_svc),
                        .test_component_svc = std::move(args.test_component_svc),
                        .ns = std::move(args.ns),
                        .test_metadata = std::move(args.test_metadata),
                        .request = std::move(controller),
                        .dispatcher = dispatcher_},
      [this](TestComponent* ptr) {
        auto it = test_components_.find(ptr);
        if (it != test_components_.end()) {
          test_components_.erase(it);
        }
      });

  test_components_.emplace(test_component.get(), std::move(test_component));
}
