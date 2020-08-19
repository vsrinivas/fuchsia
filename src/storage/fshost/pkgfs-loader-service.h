// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_PKGFS_LOADER_SERVICE_H_
#define SRC_STORAGE_FSHOST_PKGFS_LOADER_SERVICE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/lib/loader_service/loader_service.h"
#include "src/storage/fshost/fshost-boot-args.h"

namespace devmgr {

// We bootstrap pkgfs from blobfs by using a custom loader service. This bootstrap is necessary
// because pkgfs itself is what provides "symbolic filename to blobfs merkleroot resolution" for the
// rest of the system.
//
// Kernel command line arguments with the prefix "zircon.system.pkgfs.file." define the
// file-to-merkleroot mapping that this loader service uses. For example, when the loader service
// receives a LoadObject("foo.so") request, it looks for a kernel command line argument with key
// "zircon.system.pkgfs.file.lib/foo.so". If such a key exists, its value is treated as a blobfs
// merkleroot and used to load the file from blobfs.
class PkgfsLoaderService : public loader::LoaderServiceBase {
 public:
  static std::shared_ptr<PkgfsLoaderService> Create(fbl::unique_fd blob_dir,
                                                    std::shared_ptr<FshostBootArgs> boot_args);

  zx::status<zx::vmo> LoadPkgfsFile(std::string path);

 private:
  PkgfsLoaderService(std::unique_ptr<async::Loop> loop, fbl::unique_fd blob_dir,
                     std::shared_ptr<FshostBootArgs> boot_args)
      : LoaderServiceBase(loop->dispatcher(), "pkgfs"),
        loop_(std::move(loop)),
        blob_dir_(std::move(blob_dir)),
        boot_args_(boot_args) {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;

  zx::status<zx::vmo> LoadBlob(std::string merkleroot);

  // This loader automatically creates and owns its own async::Loop to match historical behavior. If
  // fshost's threading model is cleaned up then PkgfsLoaderService could be changed to just accept
  // an async_dispatcher_t* for an existing loop.
  // Also, this is wrapped in unique_ptr purely because async::Loop has a deleted move constructor.
  std::unique_ptr<async::Loop> loop_;

  // PkgfsLoaderService's lifetime is tied to the pkgfs process because open connections keep the
  // loader alive (see LoaderServiceBase), so we take care to hold onto only owned state. (weak_ptr
  // would be fine too, just no raw pointers.)
  fbl::unique_fd blob_dir_;
  std::shared_ptr<FshostBootArgs> boot_args_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_PKGFS_LOADER_SERVICE_H_
