// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VFS_VFS_DISPATCHER_H_
#define LIB_MTL_VFS_VFS_DISPATCHER_H_

#include <fs/dispatcher.h>

#include <memory>
#include <set>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/mtl/vfs/vfs_handler.h"

namespace mtl {

class FXL_EXPORT VFSDispatcher : public fs::Dispatcher {
 public:
  VFSDispatcher();
  ~VFSDispatcher() override;

  mx_status_t AddVFSHandler(mx::channel channel,
                            fs::vfs_dispatcher_cb_t callback,
                            void* iostate) final;
  void Stop(VFSHandler* handler);

 private:
  std::set<VFSHandler*> handlers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VFSDispatcher);
};

}  // namespace mtl

#endif  // LIB_MTL_VFS_VFS_DISPATCHER_H_
