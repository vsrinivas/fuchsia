// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/channel.h>

#include "ftl.h"

class FtlInfo;

// Broker device wrapper.
class NandBroker {
 public:
  explicit NandBroker(const char* path);
  ~NandBroker() {}

  // Returns true on success.
  bool Initialize();

  void SetFtl(std::unique_ptr<FtlInfo> ftl);
  const FtlInfo* ftl() const { return ftl_.get(); }

  // The internal buffer can store up to n pages at a time, where n happens to
  // be the number of pages on a block. Note that regardless of the number of
  // pages on a given operation (for example, ReadPages), the data will always
  // be returned at the start of the buffer |data()|, and oob will be placed
  // at the end of the buffer (oob()). In other words, these two pointers will
  // always point to the same location for the lifetime of this object.
  char* data() const { return reinterpret_cast<char*>(mapping_.start()); }
  char* oob() const { return data() + info_.page_size * info_.pages_per_block; }

  const fuchsia_hardware_nand_Info& Info() const { return info_; }

  // The operations to perform (return true on success):
  bool Query();
  void ShowInfo() const;
  bool ReadPages(uint32_t first_page, uint32_t count) const;
  bool DumpPage(uint32_t page) const;
  bool EraseBlock(uint32_t block) const;

 private:
  // Attempts to load the broker driver, if it seems it's needed. Returns true
  // on success.
  bool LoadBroker();

  zx_handle_t channel() const { return caller_.get(); }

  const char* path_;
  fbl::unique_fd device_;
  zx::channel caller_;
  fuchsia_hardware_nand_Info info_ = {};
  fzl::OwnedVmoMapper mapping_;
  std::unique_ptr<FtlInfo> ftl_;
};
