// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_INTERNAL_FILE_H_
#define LIB_VFS_CPP_INTERNAL_FILE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/vfs/cpp/internal/node.h>
#include <stdint.h>

#include <vector>

namespace vfs {
namespace internal {

// A file object in a file system.
//
// Implements the |fuchsia.io.File| interface. Incoming connections are
// owned by this object and will be destroyed when this object is destroyed.
//
// Subclass to implement specific file semantics.
//
// See also:
//
//  * Directory, which represents directory objects.
class File : public Node {
 public:
  File();
  ~File() override;

  // Create |count| bytes of data from the file at the given |offset|.
  //
  // The data read should be copied to |out_data|, which should be empty when
  // passed as an argument. When |ReadAt| returns, |out_data| should contain no
  // more than |count| bytes.
  virtual zx_status_t ReadAt(uint64_t count, uint64_t offset, std::vector<uint8_t>* out_data);

  // Write the given |data| to the file at the given |offset|.
  //
  // Data should be copied into the file starting at the beginning of |data|.
  // If |WriteAt| returns |ZX_OK|, |out_actual| should contain the number of
  // bytes actually written to the file.
  virtual zx_status_t WriteAt(std::vector<uint8_t> data, uint64_t offset, uint64_t* out_actual);

  // Resize the file to the given |length|.
  virtual zx_status_t Truncate(uint64_t length);

  void Describe(fuchsia::io::NodeInfoDeprecated* out_info) override;
  void GetConnectionInfo(fuchsia::io::ConnectionInfo* out_info) override;

  // Returns current file length.
  //
  // All implementations should implement this.
  virtual uint64_t GetLength() = 0;

  // Returns file capacity.
  //
  // Seek() uses this to return ZX_ERR_OUT_OF_RANGE if new seek is more than
  // this value.
  virtual size_t GetCapacity();

  // Retrieve the memory backing the file.
  //
  // If |GetBackingMemory| returns |ZX_OK|, |out_vmo| should contain a valid handle to the retrieved
  // backing memory, in accordance with properties requested by |flags|.
  virtual zx_status_t GetBackingMemory(fuchsia::io::VmoFlags flags, zx::vmo* out_vmo);

 protected:
  fuchsia::io::OpenFlags GetAllowedFlags() const override;
  fuchsia::io::OpenFlags GetProhibitiveFlags() const override;

  zx_status_t CreateConnection(fuchsia::io::OpenFlags flags,
                               std::unique_ptr<Connection>* connection) override;
};

}  // namespace internal
}  // namespace vfs

#endif  // LIB_VFS_CPP_INTERNAL_FILE_H_
