// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPONENT_RUNNER_H_
#define SRC_STORAGE_BLOBFS_COMPONENT_RUNNER_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>

#include <optional>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/mount.h"

namespace blobfs {

// TODO(fxbug.dev/90698): Once everything launches blobfs as a component, delete the old Runner
// class and rename this just Runner.
//
// The Runner class *has* to be final because it calls PagedVfs::TearDown from
// its destructor which is required to ensure thread-safety at destruction time.
class ComponentRunner final : public fs::PagedVfs {
 public:
  ComponentRunner(async::Loop& loop, ComponentOptions config);

  ComponentRunner(const ComponentRunner&) = delete;
  ComponentRunner& operator=(const ComponentRunner&) = delete;

  ~ComponentRunner();

  // fs::PagedVfs interface.
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) final;
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() final;

  zx::result<> ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root,
                         fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle,
                         fidl::ClientEnd<fuchsia_device_manager::Administrator> driver_admin_client,
                         zx::resource vmex_resource);
  zx::result<> Configure(std::unique_ptr<BlockDevice> device, const MountOptions& options);

 private:
  // Tell driver_manager to remove all drivers living in storage. This must be called before
  // shutting down. `callback` will be called once all drivers living in storage have been
  // unbound and removed.
  void RemoveSystemDrivers(fit::callback<void(zx_status_t)> callback);

  async::Loop& loop_;
  ComponentOptions config_;

  zx::resource vmex_resource_;

  // These are initialized when ServeRoot is called.
  fbl::RefPtr<fs::PseudoDir> outgoing_;
  fidl::WireSharedClient<fuchsia_device_manager::Administrator> driver_admin_;

  // These are created when ServeRoot is called, and are consumed by a successful call to
  // Configure. This causes any incoming requests to queue in the channel pair until we start
  // serving the directories, after we start the filesystem and the services.
  fidl::ServerEnd<fuchsia_io::Directory> svc_server_end_;
  fidl::ServerEnd<fuchsia_io::Directory> root_server_end_;

  // These are only initialized by configure after a call to the startup service.
  std::unique_ptr<Blobfs> blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPONENT_RUNNER_H_
