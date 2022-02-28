// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTD_MTD_INTERFACE_H_
#define LIB_MTD_MTD_INTERFACE_H_

#include <lib/mtd/nand-interface.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include <fbl/unique_fd.h>
#include <mtd/mtd-user.h>

namespace mtd {

// Thin wrapper around the Linux MTD interface.
class MtdInterface : public NandInterface {
 public:
  static std::unique_ptr<MtdInterface> Create(const std::string& mtd_path);

  ~MtdInterface() override = default;

  // NandInterface interface:
  uint32_t PageSize() const override;
  uint32_t BlockSize() const override;
  uint32_t OobSize() const override;
  uint32_t Size() const override;
  zx_status_t ReadOob(uint32_t offset, void* oob_bytes) override;
  zx_status_t ReadPage(uint32_t offset, void* data_bytes, uint32_t* actual) override;
  zx_status_t WritePage(uint32_t offset, const void* data_bytes, const void* oob_bytes) override;
  zx_status_t EraseBlock(uint32_t offset) override;
  zx_status_t IsBadBlock(uint32_t offset, bool* is_bad_block) override;

  DISALLOW_COPY_ASSIGN_AND_MOVE(MtdInterface);

 private:
  MtdInterface(fbl::unique_fd fd, const mtd_info_t& mtd_info);

  fbl::unique_fd fd_;
  mtd_info_t mtd_info_;
};

}  // namespace mtd

#endif  // LIB_MTD_MTD_INTERFACE_H_
