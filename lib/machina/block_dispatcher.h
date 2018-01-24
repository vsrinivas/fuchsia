// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_
#define GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_

#include <sys/types.h>

#include <fbl/unique_ptr.h>
#include <hypervisor/phys_mem.h>
#include <zircon/types.h>

namespace machina {

class BlockDispatcher {
 public:
  enum class Mode {
    RO,
    RW,
  };
  enum class DataPlane {
    FDIO,
    FIFO,
  };

  static zx_status_t Create(const char* path,
                            Mode mode,
                            DataPlane data_plane,
                            const PhysMem& phys_mem,
                            fbl::unique_ptr<BlockDispatcher>* dispatcher);

  BlockDispatcher(size_t size, bool read_only)
      : size_(size), read_only_(read_only) {}
  virtual ~BlockDispatcher() = default;

  virtual zx_status_t Flush() = 0;
  virtual zx_status_t Read(off_t disk_offset, void* buf, size_t size) = 0;
  virtual zx_status_t Write(off_t disk_offset,
                            const void* buf,
                            size_t size) = 0;
  virtual zx_status_t Submit() = 0;

  bool read_only() const { return read_only_; }
  size_t size() const { return size_; }

 private:
  size_t size_;
  bool read_only_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_
