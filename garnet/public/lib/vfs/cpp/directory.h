
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_DIRECTORY_H_
#define LIB_VFS_CPP_DIRECTORY_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/vfs/cpp/node.h>
#include <stdint.h>

#include <string>

namespace vfs {

// A directory object in a file system.
//
// Implements the |fuchsia.io.Directory| interface. Incoming connections are
// owned by this object and will be destroyed when this object is destroyed.
//
// Subclass to implement specific directory semantics.
//
// See also:
//
//  * File, which represents file objects.
class Directory : public Node {
 public:
  Directory();
  ~Directory() override;

  // Find an entry in this directory with the given |name|.
  //
  // The entry is returned via |out_node|. The returned entry is owned by this
  // directory.
  //
  // Returns |ZX_ERR_NOT_FOUND| if no entry exists.
  virtual zx_status_t Lookup(const std::string& name, Node** out_node) const;

  // TODO: Add support for enumeration.

  // Override that describes this object as a directory.
  void Describe(fuchsia::io::NodeInfo* out_info) override;

 protected:
  zx_status_t CreateConnection(
      uint32_t flags, std::unique_ptr<Connection>* connection) override;

  bool IsDirectory() const override;

  uint32_t GetAdditionalAllowedFlags() const override;

  uint32_t GetProhibitiveFlags() const override;
};

}  // namespace vfs

#endif  // LIB_VFS_CPP_DIRECTORY_H_
