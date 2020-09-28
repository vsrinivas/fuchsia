// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_MINFS_MINFS_H_
#define ZIRCON_TOOLS_MINFS_MINFS_H_
#include <memory>
#include <utility>

#include <fs-host/common.h>

#include "src/storage/minfs/host.h"

class MinfsCreator : public FsCreator {
 public:
  // Pass a value of '2' for the initial data blocks:
  // 1 for the reserved block, and 1 for the root directory inode.
  MinfsCreator() : FsCreator(2), dir_bytes_(0) {}

 private:
  // Parent overrides:
  zx_status_t Usage() override;
  const char* GetToolName() override { return "minfs"; }
  bool IsCommandValid(Command command) override;
  bool IsOptionValid(Option option) override;
  bool IsArgumentValid(Argument argument) override;

  zx_status_t ProcessManifestLine(FILE* manifest, const char* dir_path) override;
  zx_status_t ProcessCustom(int argc, char** argv, uint8_t* processed) override;
  zx_status_t CalculateRequiredSize(off_t* out) override;

  zx_status_t Mkfs() override;
  zx_status_t Fsck() override;
  zx_status_t UsedDataSize() override;
  zx_status_t UsedInodes() override;
  zx_status_t UsedSize() override;
  zx_status_t Add() override;
  zx_status_t Ls() override;

  // Minfs-specific methods:
  // Recursively processes the contents of a directory and adds them to the lists to be processed.
  zx_status_t ProcessEntityAndChildren(char* src, char* dst);

  // Processes the parent(s) of a directory/file and adds them to the list to be processed.
  zx_status_t ProcessParentDirectories(char* path);

  // Adds |path| to the list of directories to be processed, and calculates the space required to
  // store the directory in minfs (if necessary).
  zx_status_t ProcessDirectory(char* path);

  // Adds the |src| and |dst| files to the list of pairs to be processed, and calculates the
  // space required to store the file on minfs (if necessary).
  zx_status_t ProcessFile(char* src, char* dst);

  // Calculates the space required to store a minfs directory entry for the given path.
  void ProcessDirectoryEntry(char* path);

  // Calculates the number of minfs data blocks required for a given host-side file size.
  zx_status_t ProcessBlocks(off_t data_size);

  // Generate a Bcache instance from fd_.
  zx_status_t GenerateBcache(std::unique_ptr<minfs::Bcache>* out);

  // "Mount" the minfs partition using the host-side emu_ interface.
  zx_status_t MountMinfs();

  // Number of bytes required for all directory entries (used on create with manifest).
  size_t dir_bytes_;

  // List of all directories that may need to be created or ls'd.
  fbl::Vector<fbl::String> dir_list_;

  // List of all files to be copied from one place to another.
  // Each pair in the list represents a <source> path, and a <dest> path.
  fbl::Vector<std::pair<fbl::String, fbl::String>> file_list_;
};

#endif  // ZIRCON_TOOLS_MINFS_MINFS_H_
