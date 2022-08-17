// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_INSPECT_MANAGER_H_
#define SRC_STORAGE_FSHOST_INSPECT_MANAGER_H_

#include <lib/inspect/cpp/inspector.h>

#include <map>
#include <optional>
#include <queue>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace fshost {

// Utility to open a directory at the given `path` under `root`. The resulting channel handle will
// be in `result`. The returned `status` indicates whether the operation was successful or not.
zx_status_t OpenNode(fidl::UnownedClientEnd<fuchsia_io::Directory> root, const std::string& path,
                     uint32_t mode, fidl::ClientEnd<fuchsia_io::Node>* result);

// Management of fshost inspect data.
class FshostInspectManager {
 public:
  FshostInspectManager() = default;
  ~FshostInspectManager() = default;

  // Returns the diagnostics directory where inspect data is contained.
  fbl::RefPtr<fs::PseudoDir> Initialize(async_dispatcher* dispatcher);

  // Creates a lazy node which serves stats about the given |root|.
  void ServeStats(std::string name, fidl::ClientEnd<fuchsia_io::Directory> root);

  const inspect::Inspector& inspector() const { return inspector_; }

  void LogCorruption(fs_management::DiskFormat format);

  // Used to log the status of filesystem migrations (minfs to fxfs).
  void LogMigrationStatus(zx_status_t status);

 private:
  inspect::Inspector inspector_;

  // Node which contains counters for all filesystem corruption events. Will be lazily created
  // when the first corruption is reported via |LogCorruption|.
  std::optional<inspect::Node> corruption_node_;
  // Mapping of filesystem type to the Inspect properties keeping track of the corruption counts.
  std::map<fs_management::DiskFormat, inspect::UintProperty> corruption_events_;

  // If minfs to fxfs migration fails at boot time, this node will hold the reason.
  // This will only be set when a device boots with minfs and attempts to migrate to fxfs via the
  // disk-based migration path.
  std::optional<inspect::Node> migration_status_node_;
  std::optional<inspect::IntProperty> migration_status_;

  // Fills information about the size of files and directories under the given `root` under the
  // given `node` and emplaces it in the given `inspector`. Returns the total size of `root`.
  void FillFileTreeSizes(fidl::ClientEnd<fuchsia_io::Directory> root, inspect::Node node,
                         inspect::Inspector* inspector);

  // Queries the filesystem about stats of the given `root` and stores them in the given `inspector`
  void FillStats(fidl::UnownedClientEnd<fuchsia_io::Directory> root, inspect::Inspector* inspector);
};

// A directory entry returned by `DirectoryEntriesIterator`
struct DirectoryEntry {
  // The name of the entry.
  std::string name;
  // A handle to the node this entry represents.
  fidl::ClientEnd<fuchsia_io::Node> node;
  // If the entry its a file, this contains the content size. If the entry is a directory, this will
  // be zero.
  size_t size = 0;
  // Whether the entry is a directory or not.
  bool is_dir = false;
};

// Utility to lazily iterate over the entries of a directory.
class DirectoryEntriesIterator {
 public:
  // Create a new lazy iterator.
  explicit DirectoryEntriesIterator(fidl::ClientEnd<fuchsia_io::Directory> directory);

  // Get the next entry. If there's no more entries left (it finished), returns std::nullopt
  // forever.
  std::optional<DirectoryEntry> GetNext();

  bool finished() const { return finished_; }

 private:
  // The directory which entries will be retrieved.
  fidl::ClientEnd<fuchsia_io::Directory> directory_;
  // Pending entries to return.
  std::queue<std::string> pending_entries_;
  // Whether or not the iterator has finished.
  bool finished_ = false;

  // Creates a `DirectoryEntry`. If it fails to retrieve the entry `entry_name` attributes,
  // returns `std::nullopt`.
  std::optional<DirectoryEntry> MaybeMakeEntry(const std::string& entry_name);

  // Reads the next set of dirents and loads them into `pending_entries_`.
  void RefreshPendingEntries();
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_INSPECT_MANAGER_H_
