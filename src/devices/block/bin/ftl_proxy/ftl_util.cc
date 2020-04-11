// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/bin/ftl_proxy/ftl_util.h"

#include <dirent.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

#include <fbl/unique_fd.h>

namespace ftl_proxy {
namespace {

fit::result<std::string, zx_status_t> GetTopologicalPath(std::string_view path) {
  zx::channel device, device_service;
  zx_status_t status;
  if ((status = zx::channel::create(0, &device, &device_service)) != ZX_OK) {
    return fit::error(status);
  }

  if ((status = fdio_service_connect(path.data(), device_service.release())) != ZX_OK) {
    return fit::error(status);
  }

  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(device.borrow());
  status = resp.status();
  if (status != ZX_OK) {
    return fit::error(status);
  }

  if (resp->result.is_err()) {
    return fit::error(resp->result.err());
  }

  const auto& r = resp->result.response();
  if (r.path.size() > PATH_MAX) {
    return fit::error(ZX_ERR_INTERNAL);
  }
  return fit::ok(std::string(r.path.data(), r.path.size()));
}

}  // namespace

std::string GetFtlTopologicalPath(std::string_view device_class_path, zx::duration max_wait) {
  struct WatcherArgs {
    std::string root_path;
    std::string topological_path;
  };

  WatcherArgs watcher_args = {
      .root_path = std::string(device_class_path.data(), device_class_path.length()),
      .topological_path = ""};

  auto ftl_filter = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    auto* watcher_args = reinterpret_cast<WatcherArgs*>(cookie);
    std::string basename(watcher_args->root_path);
    basename.append("/").append(fn);
    auto topological_path_result = GetTopologicalPath(basename);
    if (topological_path_result.is_error()) {
      // Keep looking.
      return ZX_OK;
    }
    auto topological_path = topological_path_result.take_value();
    size_t pos = topological_path.rfind("/ftl");
    // Found FTL along this path, extract the path and return.
    if (pos != std::string::npos) {
      watcher_args->topological_path = topological_path.substr(0, pos + strlen("/ftl"));
      return ZX_ERR_STOP;
    }

    return ZX_OK;
  };

  fbl::unique_fd path_fd(open(device_class_path.data(), O_RDWR));
  if (!path_fd.is_valid()) {
    return "";
  }

  // Give up after max_delay if the FTL never showed up.
  zx_status_t status;
  if ((status = fdio_watch_directory(path_fd.get(), ftl_filter, zx::deadline_after(max_wait).get(),
                                     &watcher_args)) != ZX_ERR_STOP) {
    return "";
  }

  auto root_at = device_class_path.rfind("/class");
  if (root_at == std::string_view::npos) {
    return "";
  }
  // TODO(fxb/39761): Remove when there is an alternative without this assumption.
  // Assumption: Device rooted under '/dev'.
  watcher_args.topological_path.replace(0, strlen("/dev"), device_class_path.data(), root_at);
  return std::move(watcher_args.topological_path);
}

zx::vmo GetFtlInspectVmo(std::string_view ftl_path) {
  fbl::unique_fd ftl_fd(open(ftl_path.data(), O_RDWR));
  if (!ftl_fd.is_valid()) {
    return zx::vmo();
  }
  fdio_cpp::UnownedFdioCaller caller(ftl_fd.get());

  auto r = llcpp::fuchsia::hardware::block::Ftl::Call::GetVmo(caller.channel());
  if (r.status() != ZX_OK || r->result.is_err()) {
    return zx::vmo();
  }
  return std::move(r->result.mutable_response().vmo);
}

std::optional<uint64_t> GetDeviceWearCount(const zx::vmo& inspect_vmo) {
  auto hierarchy = inspect::ReadFromVmo(inspect_vmo).take_value();
  const auto* wear_count_prop =
      hierarchy.node().get_property<inspect::UintPropertyValue>("wear_count");
  if (wear_count_prop == nullptr) {
    return std::nullopt;
  }
  return wear_count_prop->value();
}

}  // namespace ftl_proxy
