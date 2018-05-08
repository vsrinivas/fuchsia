// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_FS_HACK_FILESYSTEM_H_
#define LIB_ESCHER_FS_HACK_FILESYSTEM_H_

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lib/fxl/memory/ref_counted.h"

namespace escher {

class HackFilesystem;
using HackFilesystemPtr = fxl::RefPtr<HackFilesystem>;
using HackFileContents = std::string;
using HackFilePath = std::string;
using HackFilePathSet = std::unordered_set<HackFilePath>;
class HackFilesystemWatcher;
using HackFilesystemWatcherFunc = std::function<void(HackFilePath)>;

// TODO(SCN-664): Placeholder class to allow development of subsystems that will
// eventually rely upon filesystem functionality, in particular watching and
// notifying listeners of changed files.
class HackFilesystem : public fxl::RefCountedThreadSafe<HackFilesystem> {
 public:
  HackFilesystem();
  ~HackFilesystem();

  // Return the contents of the file, which can be empty if the path doesn't
  // exist (HackFilesystem doesn't distinguish between empty and non-existent
  // files).
  HackFileContents ReadFile(const HackFilePath& path) const;

  // Set the file contents and notify watchers of the change.
  void WriteFile(const HackFilePath& path, HackFileContents new_contents);

  // The watcher will be notified whenever any of the paths that it cares
  // about change.  To stop watching, simply release the unique_ptr.
  std::unique_ptr<HackFilesystemWatcher> RegisterWatcher(
      HackFilesystemWatcherFunc func);

  // Load the specified files from the real filesystem.  When running on Fuchsia
  // these files are read from the package filesystem; a prefix of "/pkg/data/"
  // is prepended to each path.  On Linux, the files are assumed to be in a
  // subdirectory of $FUCHSIA_DIR/garnet/public/lib/escher/, and we furthermore
  // assume that $PWD == $FUCHSIA_DIR.
  bool InitializeWithRealFiles(std::vector<HackFilePath> paths);

 private:
  friend class HackFilesystemWatcher;

  std::unordered_map<HackFilePath, HackFileContents> files_;
  std::unordered_set<HackFilesystemWatcher*> watchers_;
};

// Allows clients to be notified about changes in the specified files.  There is
// no public constructor; instances of HackFilesystemWatcher must be obtained
// via HackFilesystem::RegisterWatcher().
class HackFilesystemWatcher final {
 public:
  ~HackFilesystemWatcher();

  // Start receiving notifications when the file identified by |path| changes.
  void AddPath(HackFilePath path) { paths_to_watch_.insert(std::move(path)); }

  // Read the contents of the specified file, and receive notifications if it
  // subsequently changes.
  HackFileContents ReadFile(const HackFilePath& path) {
    AddPath(path);
    return filesystem_->ReadFile(path);
  }

  // Return true if notifications will be received when |path| changes.
  bool IsWatchingPath(const HackFilePath& path) {
    return paths_to_watch_.find(path) != paths_to_watch_.end();
  }

  // Clear watcher to the default state; no notifications will be received until
  // paths are added by calling AddPath() or ReadFile().
  void ClearPaths() { paths_to_watch_.clear(); }

 private:
  friend class HackFilesystem;

  explicit HackFilesystemWatcher(HackFilesystem* filesystem,
                                 HackFilesystemWatcherFunc callback);

  HackFilesystem* const filesystem_;
  HackFilesystemWatcherFunc callback_;
  HackFilePathSet paths_to_watch_;
};

}  // namespace escher

#endif  // LIB_ESCHER_FS_HACK_FILESYSTEM_H_
