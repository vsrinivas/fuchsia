// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/watcher.h>
#include <stdio.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <inet6/netifc-discover.h>

typedef struct netifc_cb_ctx {
  const char* dirname;
  zx_handle_t* interface;
  const char* topological_path;
  uint8_t netmac[6];
  uint16_t netmtu;
} netifc_cb_ctx_t;

static zx_status_t netifc_open_cb(int dirfd, int event, const char* filename, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  netifc_cb_ctx_t* ctx = (netifc_cb_ctx_t*)cookie;
  printf("netifc: ? %s/%s\n", ctx->dirname, filename);

  int fd;
  if ((fd = openat(dirfd, filename, O_RDWR)) < 0) {
    return ZX_OK;
  }

  zx_handle_t netsvc = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_get_service_handle(fd, &netsvc);
  if (status != ZX_OK) {
    goto fail_close_svc;
  }

  if (ctx->interface != NULL) {
    *(ctx->interface) = netsvc;
  }
  // If an interface was specified, check the topological path of this device and reject it if it
  // doesn't match.
  if (ctx->topological_path != NULL) {
    const char* interface = ctx->topological_path;
    char buf[1024];
    size_t actual_len;
    auto resp =
        ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(zx::unowned_channel(netsvc));
    status = resp.status();
    if (status == ZX_OK) {
      if (resp->result.is_err()) {
        status = resp->result.err();
      } else {
        auto& r = resp->result.response();
        actual_len = r.path.size();
        if (actual_len > 1024) {
          goto fail_close_svc;
        }
        memcpy(buf, r.path.data(), r.path.size());
        status = ZX_OK;
      }
    }
    if (status != ZX_OK) {
      goto fail_close_svc;
    }
    buf[actual_len] = 0;

    const char* topo_path = buf;
    // Skip the instance sigil if it's present in either the topological path or the given
    // interface path.
    if (topo_path[0] == '@')
      topo_path++;
    if (interface[0] == '@')
      interface++;

    if (strncmp(topo_path, interface, sizeof(buf))) {
      goto fail_close_svc;
    }
  }

  fuchsia_hardware_ethernet_Info info;
  if (fuchsia_hardware_ethernet_DeviceGetInfo(netsvc, &info) != ZX_OK) {
    goto fail_close_svc;
  }
  if (info.features & fuchsia_hardware_ethernet_Features_WLAN) {
    // Don't run netsvc for wireless network devices
    goto fail_close_svc;
  }
  memcpy(ctx->netmac, info.mac.octets, sizeof(ctx->netmac));
  ctx->netmtu = static_cast<uint16_t>(info.mtu);

  printf("netsvc: using %s/%s\n", ctx->dirname, filename);

  // stop polling
  return ZX_ERR_STOP;

fail_close_svc:
  zx_handle_close(netsvc);
  netsvc = ZX_HANDLE_INVALID;

  return ZX_OK;
}

zx_status_t netifc_discover(const char* ethdir, const char* topological_path,
                            zx_handle_t* interface, uint8_t netmac[6]) {
  int dirfd;
  if ((dirfd = open(ethdir, O_DIRECTORY | O_RDONLY)) < 0) {
    return -1;
  }

  netifc_cb_ctx_t ctx = {
      .dirname = ethdir,
      .interface = interface,
      .topological_path = topological_path,
      .netmac = {},
      .netmtu = 0,
  };
  zx_status_t status = fdio_watch_directory(dirfd, netifc_open_cb, ZX_TIME_INFINITE, (void*)&ctx);
  close(dirfd);

  // callback returns STOP if it finds and successfully
  // opens a network interface
  if (status != ZX_ERR_STOP) {
    return -1;
  }

  memcpy(netmac, ctx.netmac, 6);
  return ZX_OK;
}
