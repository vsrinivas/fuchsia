// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_UTILS_H_
#define SRC_STORAGE_LIB_PAVER_UTILS_H_

#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <memory>

#include <fbl/function.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>

namespace paver {

// This class pauses the block watcher when it is Create()d, and
// resumes it when the destructor is called.
class BlockWatcherPauser {
 public:
  BlockWatcherPauser(BlockWatcherPauser&& other)
      : watcher_(std::move(other.watcher_)), valid_(other.valid_) {
    other.valid_ = false;
  }
  // Destructor for the pauser, which automatically resumes the watcher.
  ~BlockWatcherPauser();

  // This is the function used for creating the BlockWatcherPauser.
  static zx::status<BlockWatcherPauser> Create(const zx::channel& svc_root);

 private:
  // Create a new Pauser. This should immediately be followed by a call to Pause().
  BlockWatcherPauser(zx::channel chan)
      : watcher_(llcpp::fuchsia::fshost::BlockWatcher::SyncClient(std::move(chan))),
        valid_(false) {}
  zx::status<> Pause();

  llcpp::fuchsia::fshost::BlockWatcher::SyncClient watcher_;
  bool valid_;
};

// Helper function to auto-deduce type.
template <typename T>
std::unique_ptr<T> WrapUnique(T* ptr) {
  return std::unique_ptr<T>(ptr);
}

zx::status<zx::channel> OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                                      fbl::Function<bool(const zx::channel&)> should_filter_file,
                                      zx_duration_t timeout);

zx::status<zx::channel> OpenBlockPartition(const fbl::unique_fd& devfs_root,
                                           const uint8_t* unique_guid, const uint8_t* type_guid,
                                           zx_duration_t timeout);

zx::status<zx::channel> OpenSkipBlockPartition(const fbl::unique_fd& devfs_root,
                                               const uint8_t* type_guid, zx_duration_t timeout);

bool HasSkipBlockDevice(const fbl::unique_fd& devfs_root);

// Attempts to open and overwrite the first block of the underlying
// partition. Does not rebind partition drivers.
//
// At most one of |unique_guid| and |type_guid| may be nullptr.
zx::status<> WipeBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                                const uint8_t* type_guid);

zx::status<> IsBoard(const fbl::unique_fd& devfs_root, fbl::StringPiece board_name);

zx::status<> IsBootloader(const fbl::unique_fd& devfs_root, fbl::StringPiece vendor);

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_UTILS_H_
