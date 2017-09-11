// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_FARFS_FARFS_H_
#define APPLICATION_LIB_FARFS_FARFS_H_

#include <mx/channel.h>
#include <mx/vmo.h>
#include <vmofs/vmofs.h>

#include <memory>

#include "garnet/lib/far/archive_reader.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/mtl/vfs/vfs_dispatcher.h"

namespace archive {

class FileSystem {
 public:
  explicit FileSystem(mx::vmo vmo);
  ~FileSystem();

  // Serves a directory containing the contents of the archive on the given
  // channel.
  //
  // Returns true on success.
  bool Serve(mx::channel channel);

  // Returns a channel that speaks the directory protocol and contains the files
  // from the archive.
  mx::channel OpenAsDirectory();

  // Returns the contents of the the given path as a VMO.
  //
  // The VMO is a copy-on-write clone of the contents of the file, which means
  // writes to the VMO do not mutate the data in the underlying archive.
  mx::vmo GetFileAsVMO(fxl::StringView path);

  // Returns the contents of the the given path as a string.
  bool GetFileAsString(fxl::StringView path, std::string* result);

 private:
  void CreateDirectory();

  // The owning reference to the vmo is stored inside |reader_| as a file
  /// descriptor.
  mx_handle_t vmo_;
  fs::Vfs vfs_;
  mtl::VFSDispatcher dispatcher_;
  std::unique_ptr<ArchiveReader> reader_;
  fbl::RefPtr<vmofs::VnodeDir> directory_;
};

}  // namespace archive

#endif  // APPLICATION_LIB_FARFS_FARFS_H_
