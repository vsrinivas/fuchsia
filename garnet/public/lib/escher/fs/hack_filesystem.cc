// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/fs/hack_filesystem.h"

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"

#ifdef __Fuchsia__
#include "lib/escher/fs/fuchsia_data_source.h"
namespace escher {
HackFilesystemPtr HackFilesystem::New(
    const fbl::RefPtr<fs::PseudoDir>& root_dir) {
  return fxl::MakeRefCounted<FuchsiaDataSource>(root_dir);
}
HackFilesystemPtr HackFilesystem::New() {
  return fxl::MakeRefCounted<FuchsiaDataSource>();
}
}  // namespace escher
#else
#include "lib/escher/fs/linux_data_source.h"
namespace escher {
HackFilesystemPtr HackFilesystem::New() {
  return fxl::MakeRefCounted<LinuxDataSource>();
}
}  // namespace escher
#endif

namespace escher {

HackFilesystem::~HackFilesystem() { FXL_DCHECK(watchers_.size() == 0); }

HackFileContents HackFilesystem::ReadFile(const HackFilePath& path) const {
  auto it = files_.find(path);
  if (it != files_.end()) {
    return it->second;
  }
  return "";
}

void HackFilesystem::WriteFile(const HackFilePath& path,
                               HackFileContents new_contents) {
  files_[path] = std::move(new_contents);
  for (auto w : watchers_) {
    if (w->IsWatchingPath(path)) {
      w->callback_(path);
    }
  }
}

std::unique_ptr<HackFilesystemWatcher> HackFilesystem::RegisterWatcher(
    HackFilesystemWatcherFunc func) {
  // Private constructor, so cannot use std::make_unique.
  auto watcher = new HackFilesystemWatcher(this, std::move(func));
  return std::unique_ptr<HackFilesystemWatcher>(watcher);
}

HackFilesystemWatcher::HackFilesystemWatcher(HackFilesystem* filesystem,
                                             HackFilesystemWatcherFunc callback)
    : filesystem_(filesystem), callback_(std::move(callback)) {
  filesystem_->watchers_.insert(this);
}

HackFilesystemWatcher::~HackFilesystemWatcher() {
  size_t erased = filesystem_->watchers_.erase(this);
  FXL_DCHECK(erased == 1);
}

bool HackFilesystem::LoadFile(HackFilesystem* fs, const HackFilePath& prefix,
                              const HackFilePath& path) {
  std::string contents;
  if (files::ReadFileToString(prefix + path, &contents)) {
    fs->WriteFile(path, contents);
    return true;
  }
  FXL_LOG(WARNING) << "Failed to read file: " << prefix + path;
  return false;
}

}  // namespace escher
