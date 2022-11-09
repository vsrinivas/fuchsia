// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/vfs/cpp/composed_service_dir.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <filesystem>

namespace {

constexpr char kServiceDirectory[] = "/svc";
constexpr char kNetstackExposedDir[] =
    "/hub-v2/children/core/children/network/children/netstack/out/svc";
constexpr const char* kPacketSocketProviderName =
    fidl::DiscoverableProtocolName<fuchsia_posix_socket_packet::Provider>;

}  // namespace

// Attempts to make a packet socket provider available to this program if not
// already available.
//
// The packet socket provider exposed by the core realm's netstack is used if it
// is available.
__attribute__((constructor)) void init_packet_socket_provider() {
  if (std::filesystem::exists(std::filesystem::path(kServiceDirectory) /
                              kPacketSocketProviderName)) {
    // Packet socket provider is already available.
    return;
  }

  zx::result netstack_exposed_dir = component::OpenServiceRoot(kNetstackExposedDir);
  switch (zx_status_t status = netstack_exposed_dir.status_value(); status) {
    case ZX_OK:
      break;
    case ZX_ERR_NOT_FOUND:
      // Most likely in the non-root realm.
      return;
    default:
      ZX_PANIC("Failed to open netstack exposed directory at %s: %s", kNetstackExposedDir,
               zx_status_get_string(status));
  }

  static vfs::ComposedServiceDir composed_svc_dir;

  // Our composed service directory should be a superset of the default service
  // directory.
  {
    zx::result result = component::OpenServiceRoot();
    ZX_ASSERT_MSG(result.is_ok(), "Failed to open root service directory: %s",
                  result.status_string());
    // TODO(https://fxbug.dev/72980): Avoid this type-unsafe conversion.
    composed_svc_dir.set_fallback(
        fidl::InterfaceHandle<fuchsia::io::Directory>(result->TakeChannel()));
  }

  // Add the packet socket provider service to our composed service directory
  // by reaching into netstack's exposed directory via hub(-v2).
  //
  // https://fuchsia.dev/fuchsia-src/concepts/components/v2/hub?hl=en
  composed_svc_dir.AddService(
      kPacketSocketProviderName,
      std::make_unique<vfs::Service>(
          [netstack_exposed_dir = std::move(netstack_exposed_dir.value())](
              zx::channel request, async_dispatcher_t* dispatcher) mutable {
            zx::result result = component::ConnectAt(
                netstack_exposed_dir.borrow(),
                fidl::ServerEnd<fuchsia_posix_socket_packet::Provider>(std::move(request)));
            ZX_ASSERT_MSG(result.is_ok(), "Failed to connect to packet socker provider: %s",
                          result.status_string());
          }));

  static async::Loop composed_svc_dir_loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Replace the default service directory with our composed service directory
  // to make packet socket provider available to the program and start serving
  // requests to the composed service directory.
  {
    zx_status_t status;
    zx::channel client, server;
    ZX_ASSERT_MSG((status = zx::channel::create(0, &client, &server)) == ZX_OK,
                  "Failed to create channels: %s", zx_status_get_string(status));

    // TODO(https://fxbug.dev/77059): Drop writable right.
    ZX_ASSERT_MSG(
        (status = composed_svc_dir.Serve(
             fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE |
                 fuchsia::io::OpenFlags::DIRECTORY,
             std::move(server), composed_svc_dir_loop.dispatcher())) == ZX_OK,
        "Failed to start serving requsts for composed service directory: %s",
        zx_status_get_string(status));

    fdio_ns_t* ns;
    ZX_ASSERT_MSG((status = fdio_ns_get_installed(&ns)) == ZX_OK,
                  "Failed to get installed namespace: %s", zx_status_get_string(status));
    ZX_ASSERT_MSG((status = fdio_ns_unbind(ns, kServiceDirectory)) == ZX_OK,
                  "Failed to unbind svc: %s", zx_status_get_string(status));
    ZX_ASSERT_MSG((status = fdio_ns_bind(ns, kServiceDirectory, client.release())) == ZX_OK,
                  "Failed to bind svc: %s", zx_status_get_string(status));
  }

  zx_status_t status;
  ZX_ASSERT_MSG((status = composed_svc_dir_loop.StartThread()) == ZX_OK,
                "Failed to start async loop thread: %s", zx_status_get_string(status));
}
