// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/admin-server.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/async/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/storage/fshost/filesystem-mounter.h"

namespace fshost {

fbl::RefPtr<fs::Service> AdminServer::Create(FsManager* fs_manager, async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, fs_manager](fidl::ServerEnd<fuchsia_fshost::Admin> chan) {
        zx_status_t status = fidl::BindSingleInFlightOnly(
            dispatcher, std::move(chan), std::make_unique<AdminServer>(fs_manager));
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "failed to bind admin service: " << zx_status_get_string(status);
          return status;
        }
        return ZX_OK;
      });
}

void AdminServer::Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) {
  FX_LOGS(INFO) << "received shutdown command over admin interface";
  fs_manager_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "filesystem shutdown failed: " << zx_status_get_string(status);
      completer.Close(status);
    } else {
      FX_LOGS(INFO) << "shutdown complete";
      completer.Reply();
    }
  });
}

void AdminServer::Mount(MountRequestView request, MountCompleter::Sync& completer) {
  std::string device_path;
  if (auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                       request->device.channel().borrow()))
                        ->GetTopologicalPath();
      result.status() != ZX_OK) {
    FX_LOGS(WARNING) << "Unable to get device topological path (FIDL error): "
                     << zx_status_get_string(result.status());
  } else if (result->result.is_err()) {
    FX_LOGS(WARNING) << "Unable to get device topological path: "
                     << zx_status_get_string(result->result.err());
  } else {
    device_path = result->result.response().path.get();
  }

  fbl::unique_fd fd;
  if (zx_status_t status =
          fdio_fd_create(request->device.TakeChannel().release(), fd.reset_and_get_address());
      status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  fs_management::DiskFormat df = fs_management::DetectDiskFormat(fd.get());

  const std::string name(request->name.get());
  const auto& o = request->options;
  std::string compression_algorithm;
  if (o.has_write_compression_algorithm())
    compression_algorithm = o.write_compression_algorithm().get();
  fs_management::MountOptions options = {
      .readonly = o.has_read_only() && o.read_only(),
      .verbose_mount = o.has_verbose() && o.verbose(),
      .collect_metrics = o.has_collect_metrics() && o.collect_metrics(),
      .write_compression_algorithm =
          o.has_write_compression_algorithm() ? compression_algorithm.c_str() : nullptr,
  };

  FX_LOGS(INFO) << "Mounting " << fs_management::DiskFormatString(df) << " filesystem at /mnt/"
                << name;

  async_dispatcher_t* dispatcher = async_get_default_dispatcher();

  // Launching a filesystem requirees accessing the loader which is running on the same async loop
  // that we're running on but it's only running on one thread, so if we're not careful, we'll end
  // up with a deadlock.  To avoid this, spawn a separate thread.  Unfortunately, this isn't
  // thread-safe if we're shutting down, but since mounting is a debug only thing for now, we don't
  // worry about it.
  std::thread thread([device_path = std::move(device_path), name = std::move(name),
                      completer = completer.ToAsync(), options = std::move(options),
                      fd = std::move(fd), df = std::move(df), fs_manager = fs_manager_,
                      dispatcher]() mutable {
    auto mounted_filesystem_or =
        fs_management::Mount(std::move(fd), nullptr, df, options, launch_logs_async);
    if (mounted_filesystem_or.is_error()) {
      FX_LOGS(WARNING) << "Mount failed: " << mounted_filesystem_or.status_string();
      completer.ReplyError(mounted_filesystem_or.error_value());
      return;
    }

    // fs_manager isn't thread-safe, so we have to post back on to the async loop to attach the
    // mount.
    async::PostTask(dispatcher, [device_path = std::move(device_path),
                                 export_root = std::move(*mounted_filesystem_or).TakeExportRoot(),
                                 name = std::move(name), fs_manager,
                                 completer = std::move(completer)]() mutable {
      if (zx_status_t status = fs_manager->AttachMount(device_path, std::move(export_root), name);
          status != ZX_OK) {
        FX_LOGS(WARNING) << "Failed to attach mount: " << zx_status_get_string(status);
        completer.ReplyError(status);
        return;
      }
      completer.ReplySuccess();
    });
  });

  thread.detach();
}

void AdminServer::Unmount(UnmountRequestView request, UnmountCompleter::Sync& completer) {
  FX_LOGS(INFO) << "Unmounting " << request->name.get();
  if (zx_status_t status = fs_manager_->DetachMount(request->name.get()); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to unmount: " << zx_status_get_string(status);
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void AdminServer::GetDevicePath(GetDevicePathRequestView request,
                                GetDevicePathCompleter::Sync& completer) {
  if (auto device_path_or = fs_manager_->GetDevicePath(request->fs_id); device_path_or.is_error()) {
    completer.ReplyError(device_path_or.status_value());
  } else {
    completer.ReplySuccess(fidl::StringView::FromExternal(*device_path_or));
  }
}

}  // namespace fshost
