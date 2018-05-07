// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FARFS_FILE_SYSTEM_H_
#define GARNET_LIB_FARFS_FILE_SYSTEM_H_

#include <fs/synchronous-vfs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <vmofs/vmofs.h>

#include <memory>

#include "garnet/lib/far/archive_reader.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/files/unique_fd.h"

namespace archive {

class FileSystem {
 public:
  explicit FileSystem(zx::vmo vmo);
  ~FileSystem();

  // Serves a directory containing the contents of the archive on the given
  // channel.
  //
  // Returns true on success.
  bool Serve(zx::channel channel);

  // Returns a channel that speaks the directory protocol and contains the files
  // from the archive.
  zx::channel OpenAsDirectory();

  // Returns true if a file exists at the given path.
  bool IsFile(fxl::StringView path);

  // Returns the contents of the the given path as a VMO.
  //
  // The VMO is a copy-on-write clone of the contents of the file, which means
  // writes to the VMO do not mutate the data in the underlying archive.
  fsl::SizedVmo GetFileAsVMO(fxl::StringView path);

  // Returns the contents of the the given path as a string.
  bool GetFileAsString(fxl::StringView path, std::string* result);

 private:
  void CreateDirectory();

  // The owning reference to the vmo is stored inside |reader_| as a file
  /// descriptor.
  zx_handle_t vmo_;
  fs::SynchronousVfs vfs_;
  std::unique_ptr<ArchiveReader> reader_;
  fbl::RefPtr<vmofs::VnodeDir> directory_;
};

}  // namespace archive

#endif  // GARNET_LIB_FARFS_FILE_SYSTEM_H_
