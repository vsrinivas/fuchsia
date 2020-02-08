// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_H_
#define LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_H_

#include <lib/mtd/nand-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-interface.h>

#include <memory>

#include <fbl/macros.h>

namespace nand_rs {

class NandRedundantStorage : public NandRedundantStorageInterface {
 public:
  static std::unique_ptr<NandRedundantStorage> Create(std::unique_ptr<mtd::NandInterface> iface);

  virtual ~NandRedundantStorage() {}

  // NandRedundantStorageInterface interface:
  zx_status_t WriteBuffer(const std::vector<uint8_t>& buffer, uint32_t num_copies,
                          uint32_t* num_copies_written, bool skip_recovery_header = false) override;
  zx_status_t ReadToBuffer(std::vector<uint8_t>* out_buffer, bool skip_recovery_header = false,
                           size_t file_size = 0) override;

  DISALLOW_COPY_ASSIGN_AND_MOVE(NandRedundantStorage);

 private:
  explicit NandRedundantStorage(std::unique_ptr<mtd::NandInterface> iface);

  std::unique_ptr<mtd::NandInterface> iface_;
};

}  // namespace nand_rs

#endif  // LIB_NAND_REDUNDANT_STORAGE_NAND_REDUNDANT_STORAGE_H_
