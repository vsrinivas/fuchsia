// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/run-benchmark.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.sys/cpp/wire.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/llcpp/object_view.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

namespace storage_benchmark {

namespace {

class ComponentControllerEventHandler
    : public fidl::WireSyncEventHandler<fuchsia_sys::ComponentController> {
 public:
  void OnTerminated(
      ::fidl::WireEvent<::fuchsia_sys::ComponentController::OnTerminated>* event) override {
    zx_status_t status;
    switch (event->termination_reason) {
      case fuchsia_sys::TerminationReason::kExited:
        if (event->return_code != 0) {
          fprintf(stderr, "Benchmark exited abnormally: %ld\n", event->return_code);
          status = ZX_ERR_INTERNAL;
        } else {
          status = ZX_OK;
        }
        break;
      case fuchsia_sys::TerminationReason::kUrlInvalid:
        fprintf(stderr, "Failed to start benchmark: %d\n",
                static_cast<int>(event->termination_reason));
        status = ZX_ERR_INVALID_ARGS;
        break;
      case fuchsia_sys::TerminationReason::kPackageNotFound:
        fprintf(stderr, "Failed to start benchmark: %d\n",
                static_cast<int>(event->termination_reason));
        status = ZX_ERR_NOT_FOUND;
        break;
      case fuchsia_sys::TerminationReason::kUnknown:
      case fuchsia_sys::TerminationReason::kInternalError:
      case fuchsia_sys::TerminationReason::kProcessCreationError:
      case fuchsia_sys::TerminationReason::kRunnerFailed:
      case fuchsia_sys::TerminationReason::kRunnerTerminated:
      case fuchsia_sys::TerminationReason::kUnsupported:
      case fuchsia_sys::TerminationReason::kRealmShuttingDown:
      case fuchsia_sys::TerminationReason::kAccessDenied:
        fprintf(stderr, "Failed to start benchmark: %d\n",
                static_cast<int>(event->termination_reason));
        status = ZX_ERR_INTERNAL;
        break;
    }
    status_ = zx::make_status(status);
  }

  zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }

  // Returns true if the component has stopped.
  bool terminated() const { return status_.has_value(); }

  // Returns an error if the component could not be started or the component stopped with an exit
  // code other than zero. Must only be called after |terminated| returns true.
  zx::status<> termination_status() {
    ZX_ASSERT(terminated());
    return status_.value();
  }

 private:
  std::optional<zx::status<>> status_;
};

}  // namespace

zx::status<> RunBenchmark(const std::string& component_url, const std::vector<std::string>& args,
                          fidl::ClientEnd<fuchsia_io::Directory> filesystem,
                          const std::string& mount_point) {
  std::vector<fidl::StringView> component_args;
  component_args.reserve(args.size());
  for (const auto& arg : args) {
    component_args.push_back(fidl::StringView::FromExternal(arg));
  }

  fidl::StringView namespace_paths[] = {fidl::StringView::FromExternal(mount_point)};
  zx::channel namespace_directories[] = {filesystem.TakeChannel()};
  fuchsia_sys::wire::FlatNamespace namespace_entries = {
      .paths = fidl::VectorView<fidl::StringView>::FromExternal(namespace_paths),
      .directories = fidl::VectorView<zx::channel>::FromExternal(namespace_directories),
  };

  auto launcher_client = service::Connect<fuchsia_sys::Launcher>();
  if (launcher_client.is_error()) {
    fprintf(stderr, "Failed to get fuchsia.sys.Launcher: %s\n", launcher_client.status_string());
    return launcher_client.take_error();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_sys::ComponentController>();
  if (endpoints.is_error()) {
    fprintf(stderr, "Failed to create endpoints: %s\n", endpoints.status_string());
    return endpoints.take_error();
  }
  fuchsia_sys::wire::LaunchInfo launch_info = {
      .url = fidl::StringView::FromExternal(component_url),
      .arguments = fidl::VectorView<fidl::StringView>::FromExternal(component_args),
      .flat_namespace =
          fidl::ObjectView<fuchsia_sys::wire::FlatNamespace>::FromExternal(&namespace_entries),
  };
  auto response = fidl::WireCall(*launcher_client)
                      ->CreateComponent(std::move(launch_info), std::move(endpoints->server));
  if (!response.ok()) {
    fprintf(stderr, "Failed to call CreateComponent: %s\n", response.status_string());
    return zx::error(response.status());
  }

  ComponentControllerEventHandler event_handler;
  while (!event_handler.terminated()) {
    event_handler.HandleOneEvent(endpoints->client);
  }
  return event_handler.termination_status();
}

}  // namespace storage_benchmark
