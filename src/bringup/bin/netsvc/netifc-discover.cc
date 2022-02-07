// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netifc-discover.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <stdio.h>

#include <fbl/unique_fd.h>

#include "src/lib/fsl/io/device_watcher.h"

namespace {

cpp17::string_view SkipInstanceSigil(cpp17::string_view v) {
  if (!v.empty() && v.at(0) == '@') {
    return v.substr(1);
  }
  return v;
}

template <typename D>
std::optional<DiscoveredInterface> get_interface_if_matching(fidl::ClientEnd<D> dev,
                                                             const std::string& filename);

template <>
std::optional<DiscoveredInterface> get_interface_if_matching(
    fidl::ClientEnd<fuchsia_hardware_ethernet::Device> dev, const std::string& filename) {
  fidl::WireResult result = fidl::WireCall(dev)->GetInfo();
  if (!result.ok()) {
    printf("netifc: failed to get Ethernet device info %s: %s\n", filename.c_str(),
           result.status_string());
    return std::nullopt;
  }
  auto& resp = result.value();
  if (resp.info.features & fuchsia_hardware_ethernet::wire::Features::kWlan) {
    return std::nullopt;
  }
  DiscoveredInterface ret = {.device = std::move(dev)};
  static_assert(sizeof(resp.info.mac.octets) == sizeof(ret.mac.x));
  std::copy(resp.info.mac.octets.begin(), resp.info.mac.octets.end(), std::begin(ret.mac.x));
  return ret;
}

template <typename D>
std::optional<DiscoveredInterface> netifc_evaluate(
    cpp17::string_view topological_path, fidl::UnownedClientEnd<fuchsia_io::Directory> dir,
    const std::string& dirname, const std::string& filename) {
  printf("netifc: ? %s/%s\n", dirname.c_str(), filename.c_str());

  fidl::ClientEnd<D> dev;
  {
    zx::status status = service::ConnectAt<D>(dir, filename.c_str());
    if (status.is_error()) {
      printf("netifc: failed to connect to %s/%s: %s\n", dirname.c_str(), filename.c_str(),
             status.status_string());
      return std::nullopt;
    }
    dev = std::move(status.value());
  }

  // If an interface was specified, check the topological path of this device and reject it if it
  // doesn't match.
  if (!topological_path.empty()) {
    // NB: We need to take a trip through a fuchsia.device ClientEnd here to
    // abide by llcpp endpoint typing.
    fidl::UnownedClientEnd<fuchsia_device::Controller> controller(dev.channel().borrow());
    fidl::WireResult result = fidl::WireCall(controller)->GetTopologicalPath();

    if (!result.ok()) {
      printf("netifc: failed to get topological path %s: %s\n", filename.c_str(),
             result.status_string());
      return std::nullopt;
    }
    auto& resp = result.value();
    if (resp.result.is_err()) {
      printf("netifc: GetTopologicalPath returned error %s: %s\n", filename.c_str(),
             zx_status_get_string(resp.result.err()));
      return std::nullopt;
    }

    cpp17::string_view topo_path = SkipInstanceSigil(resp.result.response().path.get());
    if (topo_path != topological_path) {
      return std::nullopt;
    }
  }

  std::optional result = get_interface_if_matching<D>(std::move(dev), filename);
  if (result.has_value()) {
    printf("netsvc: using %s/%s\n", dirname.c_str(), filename.c_str());
  }
  return result;
}

}  // namespace

zx::status<DiscoveredInterface> netifc_discover(const std::string& devdir,
                                                cpp17::string_view topological_path) {
  const std::string ethdir = devdir + "/class/ethernet";
  fbl::unique_fd dir(open(ethdir.c_str(), O_DIRECTORY | O_RDONLY));
  if (!dir.is_valid()) {
    printf("failed to open %s: %s\n", ethdir.c_str(), strerror(errno));
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fidl::ClientEnd<fuchsia_io::Directory> dir_channel;
  {
    zx::status status = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (status.is_error()) {
      return status.take_error();
    }
    auto& [client, server] = status.value();
    // TODO(https://fxbug.dev/77059): We shouldn't require the writable right
    // below, but service::ConnectAt (and fdio_service_connect_at) connect to
    // protocols with the writable right.
    if (zx_status_t status =
            fdio_open(ethdir.c_str(),
                      fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenRightWritable,
                      server.TakeChannel().release());
        status != ZX_OK) {
      return zx::error(status);
    }
    dir_channel = std::move(client);
  }

  topological_path = SkipInstanceSigil(topological_path);

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  std::optional<DiscoveredInterface> selected_ifc;
  std::unique_ptr ethernet_watcher = fsl::DeviceWatcher::Create(
      ethdir,
      [&ethdir, &topological_path, &dir_channel, &selected_ifc](int dir_fd,
                                                                const std::string& filename) {
        selected_ifc = netifc_evaluate<fuchsia_hardware_ethernet::Device>(
            topological_path, dir_channel.borrow(), ethdir, filename);
      },
      loop.dispatcher());

  for (;;) {
    zx_status_t status = loop.Run(zx::time::infinite(), /* once */ true);
    if (status != ZX_OK) {
      printf("run loop error: %s\n", zx_status_get_string(status));
      return zx::error(status);
    }
    if (selected_ifc.has_value()) {
      return zx::ok(std::move(selected_ifc.value()));
    }
  }
}
