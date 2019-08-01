// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_KCOUNTER_INSPECT_VMO_FILE_WITH_UPDATE_H_
#define SRC_DEVELOPER_KCOUNTER_INSPECT_VMO_FILE_WITH_UPDATE_H_

#include <inttypes.h>
#include <zircon/types.h>

#include <vector>

#include "fuchsia/kernel/cpp/fidl.h"
#include "lib/vfs/cpp/internal/file.h"
#include "lib/zx/vmo.h"

// Similar to vfs::VmoFile, but ensures that the underlying kcounter VMO data is
// updated before returning data.
class VmoFileWithUpdate final : public vfs::internal::File {
 public:
  VmoFileWithUpdate(zx::vmo vmo, size_t offset, size_t length,
                    fuchsia::kernel::CounterSyncPtr* kcounter);
  ~VmoFileWithUpdate();

  zx_status_t ReadAt(uint64_t length, uint64_t offset, std::vector<uint8_t>* out_data) override;
  void Describe(fuchsia::io::NodeInfo* out_info) override;
  uint64_t GetLength() override { return length_; }
  size_t GetCapacity() override { return length_; }
  zx_status_t GetAttr(fuchsia::io::NodeAttributes* out_attributes) const override;

  // Read-only, these aren't implemented
  zx_status_t WriteAt(std::vector<uint8_t> data, uint64_t offset, uint64_t* out_actual) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Truncate(uint64_t length) override { return ZX_ERR_NOT_SUPPORTED; }

 protected:
  vfs::NodeKind::Type GetKind() const override {
    return File::GetKind() | vfs::NodeKind::kVmo | vfs::NodeKind::kReadable;
  }

 private:
  zx_status_t Update();

  const size_t offset_;
  const size_t length_;
  zx::vmo vmo_;
  fuchsia::kernel::CounterSyncPtr* kcounter_;
};

#endif  // SRC_DEVELOPER_KCOUNTER_INSPECT_VMO_FILE_WITH_UPDATE_H_
