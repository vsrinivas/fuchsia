// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/package_updating_loader.h"

#include <fcntl.h>
#include <string>
#include <utility>

#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/substitute.h"
#include "lib/svc/cpp/services.h"

namespace sysmgr {

PackageUpdatingLoader::PackageUpdatingLoader(
    fuchsia::amber::ControlPtr amber_ctl, async_dispatcher_t* dispatcher)
    : amber_ctl_(std::move(amber_ctl)), dispatcher_(dispatcher) {}

PackageUpdatingLoader::~PackageUpdatingLoader() = default;

bool PackageUpdatingLoader::LoadComponentFromPkgfs(
    component::FuchsiaPkgUrl component_url, LoadComponentCallback callback) {
  const std::string package_name = component_url.package_name();
  auto done_cb = [this, component_url = std::move(component_url),
                  callback = std::move(callback)](std::string error) mutable {
    const std::string& pkg_path = component_url.pkgfs_dir_path();
    if (!error.empty()) {
      FXL_LOG(ERROR) << "Package update encountered unexpected error \""
                     << error << "\": " << pkg_path;
      callback(nullptr);
      return;
    }
    if (!LoadPackage(std::move(component_url), callback)) {
      FXL_LOG(ERROR) << "Package failed to load after package update: "
                     << pkg_path;
      callback(nullptr);
      return;
    }
  };
  if (package_name == "amber") {
    // Avoid infinite regression: Don't attempt to update the amber package.
    // Contacting the amber service may require starting its component, which
    // would end up back here.
    done_cb("");
    return true;
  }
  StartUpdatePackage(package_name, std::move(done_cb));
  return true;
}

void PackageUpdatingLoader::StartUpdatePackage(const std::string& package_name,
                                               DoneCallback done_cb) {
  auto cb = [this,
             done_cb = std::move(done_cb)](zx::channel reply_chan) mutable {
    ListenForPackage(std::move(reply_chan), std::move(done_cb));
  };
  amber_ctl_->GetUpdateComplete(package_name, "0", nullptr, std::move(cb));
}

namespace {
constexpr int ZXSIO_DAEMON_ERROR = ZX_USER_SIGNAL_0;
}  // namespace

void PackageUpdatingLoader::ListenForPackage(zx::channel reply_chan,
                                             DoneCallback done_cb) {
  async::Wait* wait = new async::Wait(
      reply_chan.release(),
      ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE | ZXSIO_DAEMON_ERROR,
      [done_cb = std::move(done_cb)](async_dispatcher_t* dispatcher,
                                     async::Wait* wait, zx_status_t status,
                                     const zx_packet_signal_t* signal) mutable {
        WaitForUpdateDone(dispatcher, wait, status, signal, std::move(done_cb));
      });
  zx_status_t r = wait->Begin(dispatcher_);
  if (r != ZX_OK) {
    delete wait;
    done_cb(fxl::Substitute("Failed to start waiting for package update: $0",
                            fxl::StringView(zx_status_get_string(r))));
  }
}

// static
void PackageUpdatingLoader::WaitForUpdateDone(async_dispatcher_t* dispatcher,
                                              async::Wait* wait,
                                              zx_status_t status,
                                              const zx_packet_signal_t* signal,
                                              DoneCallback done_cb) {
  if (status == ZX_OK && (signal->observed & ZXSIO_DAEMON_ERROR)) {
    // Daemon signalled an error, wait for its error message.
    const zx_handle_t reply_chan = wait->object();
    delete wait;
    wait = new async::Wait(
        reply_chan, ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
        [done_cb = std::move(done_cb)](
            async_dispatcher_t* dispatcher, async::Wait* wait,
            zx_status_t status, const zx_packet_signal_t* signal) mutable {
          FinishWaitForUpdate(dispatcher, wait, status, signal, true,
                              std::move(done_cb));
        });
    zx_status_t r = wait->Begin(dispatcher);
    if (r != ZX_OK) {
      delete wait;
      done_cb(fxl::Substitute("Failed to start waiting for package update: $0",
                              fxl::StringView(zx_status_get_string(r))));
    }
    return;
  }
  FinishWaitForUpdate(dispatcher, wait, status, signal, false,
                      std::move(done_cb));
}

// static
void PackageUpdatingLoader::FinishWaitForUpdate(
    async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
    const zx_packet_signal_t* signal, bool daemon_err, DoneCallback done_cb) {
  const zx_handle_t reply_chan = wait->object();
  delete wait;
  if (status != ZX_OK) {
    done_cb(fxl::Substitute("Failed waiting for package update: $0",
                            fxl::StringView(zx_status_get_string(status))));
    return;
  }
  if (status == ZX_OK && (signal->observed & ZX_CHANNEL_READABLE)) {
    // Read response from channel.
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes, actual_handles;
    zx_status_t r = zx_channel_read(
        reply_chan, 0, bytes, /*handles=*/nullptr, ZX_CHANNEL_MAX_MSG_BYTES,
        /*num_handles=*/0, &actual_bytes, &actual_handles);
    if (r != ZX_OK) {
      done_cb(fxl::Substitute("Error reading response from channel: $0",
                              fxl::StringView(zx_status_get_string(r))));
      return;
    }
    bytes[actual_bytes] = '\0';
    if (daemon_err) {
      // If the package daemon reported an error (for example, maybe it could
      // not access the remote server), log a warning but allow the stale
      // package to be loaded.
      FXL_LOG(WARNING) << "Package update failed. Loading package without "
                       << "update. Error: " << bytes;
    }
    done_cb("");
  } else if (status == ZX_OK && (signal->observed & ZX_CHANNEL_PEER_CLOSED)) {
    done_cb("Update response channel closed unexpectedly.");
  } else {
    done_cb(fxl::Substitute("Waiting for update failed: $0",
                            fxl::StringView(zx_status_get_string(status))));
  }
}

}  // namespace sysmgr
