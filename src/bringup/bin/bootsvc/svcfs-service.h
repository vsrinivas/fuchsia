// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_BOOTSVC_SVCFS_SERVICE_H_
#define SRC_BRINGUP_BIN_BOOTSVC_SVCFS_SERVICE_H_

#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>

#include <vector>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "util.h"

namespace bootsvc {

// A VFS used to provide services to the next process in the boot sequence.
class SvcfsService : public fbl::RefCounted<SvcfsService> {
 public:
  // Create a SvcfsService using the given |dispatcher|.
  static fbl::RefPtr<SvcfsService> Create(async_dispatcher_t* dispatcher);

  // Add a |service| named |service_name| to the VFS.
  void AddService(const char* service_name, fbl::RefPtr<fs::Service> service);

  // Create a connection to the root of the VFS.
  zx_status_t CreateRootConnection(zx::channel* out);

 private:
  explicit SvcfsService(async_dispatcher_t* dispatcher);

  SvcfsService(const SvcfsService&) = delete;
  SvcfsService(SvcfsService&&) = delete;
  SvcfsService& operator=(const SvcfsService&) = delete;
  SvcfsService& operator=(SvcfsService&&) = delete;

  fs::SynchronousVfs vfs_;
  // Root node for |vfs_|.
  fbl::RefPtr<fs::PseudoDir> root_;
};

// Create a service to retrive factory ZBI items.
fbl::RefPtr<fs::Service> CreateFactoryItemsService(async_dispatcher_t* dispatcher,
                                                   FactoryItemMap map);

// Create a service to retrieve ZBI items.
fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map, BootloaderFileMap bootloader_file_map);

}  // namespace bootsvc

#endif  // SRC_BRINGUP_BIN_BOOTSVC_SVCFS_SERVICE_H_
