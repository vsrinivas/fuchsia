// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_
#define GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_

#include <sys/types.h>
#include <vector>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace machina {

class PhysMem;

class BlockDispatcher {
 public:
  enum class Mode {
    RO,
    RW,
  };
  enum class DataPlane {
    FDIO,
    QCOW,
  };
  enum class GuidType {
    NONE,

    // Each GPT partition has 2 GUIDs, one that is unique to that specific
    // partition, and one that specifies the purpose of the partition.
    //
    // For a partial list of existing partition type GUIDs, see
    // https://en.wikipedia.org/wiki/GUID_Partition_Table#Partition_type_GUIDs
    GPT_PARTITION_GUID,
    GPT_PARTITION_TYPE_GUID,
  };
  struct Guid {
    GuidType type = GuidType::NONE;
    uint8_t bytes[16];

    // If |false|, |bytes| contains a valid GUID.
    bool empty() const { return type == GuidType::NONE; }
  };

  // Creates a new dispatcher that stores writes in RAM. Untouched blocks
  // are delegated to the provided dispatcher.
  static zx_status_t CreateVolatileWrapper(
      fbl::unique_ptr<BlockDispatcher> dispatcher,
      fbl::unique_ptr<BlockDispatcher>* out);

  static zx_status_t CreateFromPath(
      const char* path, Mode mode, DataPlane data_plane,
      const PhysMem& phys_mem, fbl::unique_ptr<BlockDispatcher>* dispatcher);

  static zx_status_t CreateFromGuid(
      const Guid& guid, zx_duration_t timeout, Mode mode, DataPlane data_plane,
      const PhysMem& phys_mem, fbl::unique_ptr<BlockDispatcher>* dispatcher);

  static zx_status_t CreateFromFd(int fd, Mode mode, DataPlane data_plane,
                                  const PhysMem& phys_mem,
                                  fbl::unique_ptr<BlockDispatcher>* dispatcher);

  BlockDispatcher(size_t size, bool read_only)
      : size_(size), read_only_(read_only) {}
  virtual ~BlockDispatcher() = default;

  virtual zx_status_t Flush() = 0;
  virtual zx_status_t Read(off_t disk_offset, void* buf, size_t size) = 0;
  virtual zx_status_t Write(off_t disk_offset, const void* buf,
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
