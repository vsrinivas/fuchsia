// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_COMPONENT_RUNNER_H_
#define SRC_STORAGE_F2FS_COMPONENT_RUNNER_H_

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace f2fs {

class ComponentRunner final : public fs::PagedVfs {
 public:
  explicit ComponentRunner(async_dispatcher_t* dispatcher);

  ComponentRunner(const ComponentRunner&) = delete;
  ComponentRunner& operator=(const ComponentRunner&) = delete;

  ~ComponentRunner();

  zx::result<> ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root,
                         fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle);
  zx::result<> Configure(std::unique_ptr<Bcache> bcache, const MountOptions& options);

  // fs::PagedVfs interface
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) final;
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() final;
  void OnNoConnections() final;

  void SetUnmountCallback(fit::closure on_unmount) { on_unmount_ = std::move(on_unmount); }

 private:
  async_dispatcher_t* dispatcher_;
  fit::closure on_unmount_;

  // These are initialized when ServeRoot is called.
  fbl::RefPtr<fs::PseudoDir> outgoing_;

  // These are created when ServeRoot is called, and are consumed by a successful call to
  // Configure. This causes any incoming requests to queue in the channel pair until we start
  // serving the directories, after we start the filesystem and the services.
  fidl::ServerEnd<fuchsia_io::Directory> svc_server_end_;
  fidl::ServerEnd<fuchsia_io::Directory> root_server_end_;

  // These are only initialized by configure after a call to the startup service.
  std::unique_ptr<F2fs> f2fs_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_COMPONENT_RUNNER_H_
