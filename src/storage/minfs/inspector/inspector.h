// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for inspecting various on-disk structures
// of a MinFS filesystem.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_H_
#define SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_H_

#include <lib/async/dispatcher.h>

#include <disk_inspector/common_types.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/storage/minfs/bcache.h"

namespace minfs {

class Inspector : public disk_inspector::DiskInspector {
 public:
  Inspector() = delete;
  Inspector(const Inspector&) = delete;
  Inspector(Inspector&&) = delete;
  Inspector& operator=(const Inspector&) = delete;
  Inspector& operator=(Inspector&&) = delete;

  explicit Inspector(async_dispatcher_t* dispatcher,
                     std::unique_ptr<block_client::BlockDevice> device)
      : dispatcher_(dispatcher), device_(std::move(device)) {}

  // DiskInspector interface:
  zx_status_t GetRoot(std::unique_ptr<disk_inspector::DiskObject>* out) final;

 private:
  // Creates root DiskObject.
  zx::status<std::unique_ptr<disk_inspector::DiskObject>> CreateRoot(std::unique_ptr<Bcache> bc);

  async_dispatcher_t* dispatcher_ = nullptr;

  // Device being inspected.
  std::unique_ptr<block_client::BlockDevice> device_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_H_
