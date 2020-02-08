// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NAND_REDUNDANT_STORAGE_FILE_NAND_REDUNDANT_STORAGE_H_
#define LIB_NAND_REDUNDANT_STORAGE_FILE_NAND_REDUNDANT_STORAGE_H_

#include <lib/nand-redundant-storage/nand-redundant-storage-interface.h>

#include <cstdint>

#include <fbl/macros.h>
#include <fbl/unique_fd.h>

namespace nand_rs {

class FileNandRedundantStorage : public NandRedundantStorageInterface {
 public:
  FileNandRedundantStorage(fbl::unique_fd file, uint32_t block_size, uint32_t page_size);
  virtual ~FileNandRedundantStorage() = default;

  uint32_t BlockSize() const;
  uint32_t PageSize() const;

  // NandRedundantStorageInterface interface:
  zx_status_t WriteBuffer(const std::vector<uint8_t>& buffer, uint32_t num_copies,
                          uint32_t* num_copies_written, bool skip_recovery_header = false) override;
  zx_status_t ReadToBuffer(std::vector<uint8_t>* out_buffer, bool skip_recovery_header = false,
                           size_t file_size = 0) override;

  DISALLOW_COPY_ASSIGN_AND_MOVE(FileNandRedundantStorage);

 private:
  fbl::unique_fd file_;

  uint32_t block_size_;
  uint32_t page_size_;
};

}  // namespace nand_rs

#endif  // LIB_NAND_REDUNDANT_STORAGE_FILE_NAND_REDUNDANT_STORAGE_H_
