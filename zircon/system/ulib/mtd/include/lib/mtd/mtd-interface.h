// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <memory>
#include <string>

#include <fbl/unique_fd.h>
#include <mtd/mtd-user.h>
#include <zircon/types.h>

#include <lib/mtd/nand-interface.h>

namespace mtd {

// Thin wrapper around the Linux MTD interface.
class MtdInterface : public NandInterface {
 public:
  static std::unique_ptr<MtdInterface> Create(const std::string& mtd_path);

  virtual ~MtdInterface() {}

  // NandInterface interface:
  uint32_t PageSize();
  uint32_t BlockSize();
  uint32_t OobSize();
  uint32_t Size();
  zx_status_t ReadOob(uint32_t offset, void* oob_bytes);
  zx_status_t ReadPage(uint32_t offset, void* data_bytes, uint32_t* actual);
  zx_status_t WritePage(uint32_t offset, const void* data_bytes, const void* oob_bytes);
  zx_status_t EraseBlock(uint32_t offset);
  zx_status_t IsBadBlock(uint32_t offset, bool* is_bad_block);

  DISALLOW_COPY_ASSIGN_AND_MOVE(MtdInterface);

 private:
  MtdInterface(fbl::unique_fd fd, const mtd_info_t& mtd_info);

  fbl::unique_fd fd_;
  mtd_info_t mtd_info_;
};

}  // namespace mtd
