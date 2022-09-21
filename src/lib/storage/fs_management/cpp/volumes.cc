// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/volumes.h"

#include <fcntl.h>
#include <fidl/fuchsia.fxfs/cpp/markers.h>
#include <fidl/fuchsia.fxfs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/markers.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <fidl/fuchsia.unknown/cpp/markers.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/status.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace fs_management {

namespace {

zx::status<> CheckExists(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                         const std::string& path) {
  // Check if the volume exists.  This way, we can return an explicit NOT_FOUND if absent.
  // TODO(fxbug.dev/93066): Check the epitaph of the call to Mount instead.
  auto endpoints_or = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints_or.is_error())
    return endpoints_or.take_error();
  auto [client, server] = std::move(*endpoints_or);
  auto res =
      fidl::WireCall(exposed_dir)
          ->Open(fuchsia_io::wire::OpenFlags::kNodeReference, fuchsia_io::wire::kModeTypeService,
                 fidl::StringView::FromExternal(path), std::move(server));
  if (!res.ok()) {
    return zx::error(res.error().status());
  }
  auto query_res = fidl::WireCall(client)->Query();
  if (!query_res.ok()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok();
}

}  // namespace

__EXPORT
zx::status<> CreateVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                          std::string_view name,
                          fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                          zx::channel crypt_client) {
  auto client = service::ConnectAt<fuchsia_fxfs::Volumes>(exposed_dir);
  if (client.is_error())
    return client.take_error();

  auto crypt = fidl::ClientEnd<fuchsia_fxfs::Crypt>(std::move(crypt_client));
  auto result = fidl::WireCall(*client)->Create(fidl::StringView::FromExternal(name),
                                                std::move(crypt), std::move(outgoing_dir));
  if (!result.ok())
    return zx::error(result.error().status());
  if (result->is_error())
    return result->take_error();

  return zx::ok();
}

__EXPORT
zx::status<> OpenVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                        std::string_view name, fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                        zx::channel crypt_client) {
  std::string path = "volumes/" + std::string(name);
  if (auto status = CheckExists(exposed_dir, path); status.is_error()) {
    return status.take_error();
  }

  auto client = service::ConnectAt<fuchsia_fxfs::Volume>(exposed_dir, path.c_str());
  if (client.is_error())
    return client.take_error();
  fuchsia_fxfs::wire::MountOptions options{
      .crypt = fidl::ClientEnd<fuchsia_fxfs::Crypt>(std::move(crypt_client)),
  };
  auto result = fidl::WireCall(*client)->Mount(std::move(outgoing_dir), std::move(options));
  if (!result.ok())
    return zx::error(result.error().status());
  if (result->is_error())
    return result->take_error();

  return zx::ok();
}

__EXPORT zx::status<> CheckVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                                  std::string_view name, zx::channel crypt_client) {
  std::string path = "volumes/" + std::string(name);
  if (auto status = CheckExists(exposed_dir, path); status.is_error()) {
    return status.take_error();
  }

  auto client = service::ConnectAt<fuchsia_fxfs::Volume>(exposed_dir, path.c_str());
  if (client.is_error())
    return client.take_error();
  fuchsia_fxfs::wire::CheckOptions options{
      .crypt = fidl::ClientEnd<fuchsia_fxfs::Crypt>(std::move(crypt_client)),
  };
  auto result = fidl::WireCall(*client)->Check(std::move(options));
  if (!result.ok())
    return zx::error(result.error().status());
  if (result->is_error())
    return result->take_error();

  return zx::ok();
}

}  // namespace fs_management
