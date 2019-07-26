// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "nand-broker.h"

class NandBroker;

class FtlInfo {
 public:
  virtual ~FtlInfo() {}

  // Stores a pointer to the provided |nand|, so this object must not outlive
  // the passed in NandBroker.
  static std::unique_ptr<FtlInfo> Factory(const NandBroker* nand);

  // Prints out basic information about the volume.
  virtual void DumpInfo() const = 0;

  // Returns true if the block is damaged.
  virtual bool IsBadBlock(uint32_t block) const = 0;

  // Returns the last block that contains FTL data. Note this is not the same
  // as the size of the FTL volume.
  virtual uint32_t LastFtlBlock() const = 0;

  // Returns true if this page is a map page, by looking at the last block
  // read by NandBroker. Note that the caller must read the whole block before
  // calling this method.
  virtual bool IsMapPage(uint32_t page) const = 0;
};

// Displays wear count information.
bool WearCounts(const NandBroker& nand);
