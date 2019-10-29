// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for inspecting various on-disk structures
// of a Blobfs filesystem.

#ifndef BLOBFS_INSPECTOR_INSPECTOR_H_
#define BLOBFS_INSPECTOR_INSPECTOR_H_

#include <lib/disk-inspector/common-types.h>

#include <block-client/cpp/block-device.h>

namespace blobfs {

class Inspector : public disk_inspector::DiskInspector {
 public:
  Inspector() = delete;
  Inspector(const Inspector&) = delete;
  Inspector(Inspector&&) = delete;
  Inspector& operator=(const Inspector&) = delete;
  Inspector& operator=(Inspector&&) = delete;

  explicit Inspector(std::unique_ptr<block_client::BlockDevice> device)
      : device_(std::move(device)) {}

  // DiskInspector interface:
  zx_status_t GetRoot(std::unique_ptr<disk_inspector::DiskObject>* out) final;

 private:
  // Device being inspected.
  std::unique_ptr<block_client::BlockDevice> device_;
};

}  // namespace blobfs

#endif  // BLOBFS_INSPECTOR_INSPECTOR_H_
