// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/root_environment_host.h"

#include <errno.h>
#include <fcntl.h>

#include <utility>

#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fxl/files/unique_fd.h"

namespace app {
namespace {

constexpr char kRootLabel[] = "root";
constexpr char kRootInfoDirName[] = "root_info_experimental";

zx::channel Mount(const char* path) {
  fxl::UniqueFD fd(open("/", O_RDONLY | O_DIRECTORY | O_ADMIN));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << "Could not open root directory: errno=" << errno;
    return zx::channel();
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0u, &client, &server);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not create server channel: status=" << status;
    return zx::channel();
  }

  size_t path_len = strlen(path);
  size_t config_size = sizeof(mount_mkdir_config_t) + path_len + 1;
  auto config = static_cast<mount_mkdir_config_t*>(malloc(config_size));
  FXL_CHECK(config);
  config->fs_root = client.release();  // takes ownership
  config->flags = MOUNT_MKDIR_FLAG_REPLACE;
  memcpy(config->name, path, path_len + 1);
  ssize_t r = ioctl_vfs_mount_mkdir_fs(fd.get(), config, config_size);
  free(config);

  if (r < 0) {
    FXL_LOG(ERROR) << "Could not mount fs at \"" << path << "\", status=" << r;
    return zx::channel();
  }
  return server;
}

}  // namespace

RootEnvironmentHost::RootEnvironmentHost(
    std::vector<std::string> application_path,
    fs::Vfs* vfs)
    : loader_(application_path), host_binding_(this), vfs_(vfs) {
  fidl::InterfaceHandle<ApplicationEnvironmentHost> host;
  host_binding_.Bind(&host);
  root_job_ =
      std::make_unique<JobHolder>(nullptr, vfs_, std::move(host), kRootLabel);

  // TODO(ZX-1036): For scaffolding purposes, mount the root information
  // directory into the devmgr root namespace so that the debug shell can
  // see it.  We'll eventually turn this inside out so there's no mount needed.
  zx::channel root_info = Mount(kRootInfoDirName);
  if (root_info) {
    zx_status_t status =
        vfs_->ServeDirectory(root_job_->info_dir(), fbl::move(root_info));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to serve info directory: status=" << status;
    }
  }
}

RootEnvironmentHost::~RootEnvironmentHost() = default;

void RootEnvironmentHost::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<ServiceProvider> environment_services) {
  service_provider_bindings_.AddBinding(this, std::move(environment_services));
}

void RootEnvironmentHost::ConnectToService(const fidl::String& interface_name,
                                           zx::channel channel) {
  if (interface_name == ApplicationLoader::Name_) {
    loader_bindings_.AddBinding(
        &loader_,
        fidl::InterfaceRequest<ApplicationLoader>(std::move(channel)));
  }
}

}  // namespace app
