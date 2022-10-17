// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_UTILS_H_
#define SRC_STORAGE_LIB_PAVER_UTILS_H_

#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.skipblock/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <memory>
#include <optional>
#include <string_view>

#include <fbl/unique_fd.h>

#include "src/lib/uuid/uuid.h"

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
  static zx::result<BlockWatcherPauser> Create(
      fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root);

 private:
  // Create a new Pauser. This should immediately be followed by a call to Pause().
  explicit BlockWatcherPauser(fidl::ClientEnd<fuchsia_fshost::BlockWatcher> chan)
      : watcher_(fidl::WireSyncClient(std::move(chan))), valid_(false) {}
  zx::result<> Pause();

  fidl::WireSyncClient<fuchsia_fshost::BlockWatcher> watcher_;
  bool valid_;
};

// Helper function to auto-deduce type.
template <typename T>
std::unique_ptr<T> WrapUnique(T* ptr) {
  return std::unique_ptr<T>(ptr);
}

// Either opens a |fuchsia.hardware.block.partition/Partition|, or
// |fuchsia.hardware.skipblock/SkipBlock|, depending on the filter rules
// defined in |should_filter_file|.
zx::result<zx::channel> OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                                      fit::function<bool(const zx::channel&)> should_filter_file,
                                      zx_duration_t timeout);

zx::result<fidl::ClientEnd<fuchsia_hardware_block_partition::Partition>> OpenBlockPartition(
    const fbl::unique_fd& devfs_root, std::optional<uuid::Uuid> unique_guid,
    std::optional<uuid::Uuid> type_guid, zx_duration_t timeout);

zx::result<fidl::ClientEnd<fuchsia_hardware_skipblock::SkipBlock>> OpenSkipBlockPartition(
    const fbl::unique_fd& devfs_root, const uuid::Uuid& type_guid, zx_duration_t timeout);

bool HasSkipBlockDevice(const fbl::unique_fd& devfs_root);

// Attempts to open and overwrite the first block of the underlying
// partition. Does not rebind partition drivers.
//
// At most one of |unique_guid| and |type_guid| may be nullptr.
zx::result<> WipeBlockPartition(const fbl::unique_fd& devfs_root,
                                std::optional<uuid::Uuid> unique_guid,
                                std::optional<uuid::Uuid> type_guid);

zx::result<> IsBoard(const fbl::unique_fd& devfs_root, std::string_view board_name);

zx::result<> IsBootloader(const fbl::unique_fd& devfs_root, std::string_view vendor);

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_UTILS_H_
