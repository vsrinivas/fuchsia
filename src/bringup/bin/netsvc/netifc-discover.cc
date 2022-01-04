// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netifc-discover.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/service/llcpp/service.h>
#include <stdio.h>

#include <fbl/unique_fd.h>

namespace {

struct netifc_cb_ctx {
  const std::string& dirname;
  cpp17::string_view topological_path;
  fidl::UnownedClientEnd<fuchsia_io::Directory> dir;
  zx::channel interface;
  mac_addr_t mac;
};

cpp17::string_view SkipInstanceSigil(cpp17::string_view v) {
  if (!v.empty() && v.at(0) == '@') {
    return v.substr(1);
  }
  return v;
}

zx_status_t netifc_open_cb(int dirfd, int event, const char* filename, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (strcmp(filename, ".") == 0) {
    return ZX_OK;
  }

  netifc_cb_ctx& ctx = *reinterpret_cast<netifc_cb_ctx*>(cookie);
  printf("netifc: ? %s/%s\n", ctx.dirname.c_str(), filename);

  fidl::ClientEnd<fuchsia_hardware_ethernet::Device> dev;
  {
    zx::status status = service::ConnectAt<fuchsia_hardware_ethernet::Device>(ctx.dir, filename);
    if (status.is_error()) {
      printf("netifc: failed to connect to %s/%s: %s\n", ctx.dirname.c_str(), filename,
             status.status_string());
      return ZX_OK;
    }
    dev = std::move(status.value());
  }

  // If an interface was specified, check the topological path of this device and reject it if it
  // doesn't match.
  if (!ctx.topological_path.empty()) {
    // NB: We need to take a trip through a fuchsia.device ClientEnd here to
    // abide by llcpp endpoint typing.
    fidl::UnownedClientEnd<fuchsia_device::Controller> controller(dev.channel().borrow());
    fidl::WireResult result = fidl::WireCall(controller)->GetTopologicalPath();

    if (!result.ok()) {
      printf("netifc: failed to get topological path %s: %s\n", filename, result.status_string());
      return ZX_OK;
    }
    auto& resp = result.value();
    if (resp.result.is_err()) {
      printf("netifc: GetTopologicalPath returned error %s: %s\n", filename,
             zx_status_get_string(resp.result.err()));
      return ZX_OK;
    }

    cpp17::string_view topo_path = SkipInstanceSigil(resp.result.response().path.get());
    if (topo_path != ctx.topological_path) {
      return ZX_OK;
    }
  }

  fidl::WireResult result = fidl::WireCall(dev)->GetInfo();
  if (!result.ok()) {
    printf("netifc: failed to get device info %s: %s\n", filename, result.status_string());
    return ZX_OK;
  }
  auto& resp = result.value();
  if (resp.info.features & fuchsia_hardware_ethernet::wire::Features::kWlan) {
    return ZX_OK;
  }

  printf("netsvc: using %s/%s\n", ctx.dirname.c_str(), filename);
  ctx.interface = std::move(dev.channel());
  static_assert(sizeof(resp.info.mac.octets) == sizeof(ctx.mac.x));
  std::copy(resp.info.mac.octets.begin(), resp.info.mac.octets.end(), std::begin(ctx.mac.x));

  // Stop polling.
  return ZX_ERR_STOP;
}

}  // namespace

zx::status<DiscoveredInterface> netifc_discover(const std::string& ethdir,
                                                cpp17::string_view topological_path) {
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

  netifc_cb_ctx ctx = {
      .dirname = ethdir,
      .topological_path = SkipInstanceSigil(topological_path),
      .dir = dir_channel.borrow(),
  };
  if (zx_status_t status = fdio_watch_directory(dir.get(), netifc_open_cb, ZX_TIME_INFINITE,
                                                static_cast<void*>(&ctx));
      status != ZX_ERR_STOP) {
    // callback returns STOP if it finds and successfully
    // opens a network interface
    ZX_ASSERT(status != ZX_OK);
    return zx::error(status);
  }
  return zx::ok(DiscoveredInterface{
      .chan = std::move(ctx.interface),
      .mac = ctx.mac,
  });
}
