// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/space_dumper.h"

#include <lib/syslog/cpp/macros.h>

#ifdef __Fuchsia__
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#endif

#include <string>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {

Minfs* SpaceDumper::minfs_ = nullptr;

namespace {

#if defined(__Fuchsia__)
constexpr uint64_t kMinSizeToDump = 1024;

// The Dirent structure passed over FIDL is different than the Minfs one.
struct __attribute__((__packed__)) FidlDirent {
  uint64_t ino;
  uint8_t name_size;
  uint8_t type;
  char name[];
};

void DumpDir(Minfs* minfs, VnodeMinfs& vnode, const std::string& path) {
  std::vector<char> buf;
  buf.resize(16384);  // Assume everything in a directory fits in this buffer.

  fs::VdirCookie cookie;
  memset(&cookie, 0, sizeof(cookie));
  size_t actual_buf_size = 0;
  if (zx_status_t status = vnode.Readdir(&cookie, buf.data(), buf.size(), &actual_buf_size);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Can't Readdir " << path << ": " << status;
    return;
  }
  buf.resize(actual_buf_size);

  size_t cursor = 0;
  while (cursor < buf.size()) {
    FidlDirent* d = reinterpret_cast<FidlDirent*>(&buf[cursor]);
    cursor += 10 + d->name_size;

    std::string file_part(d->name, d->name_size);
    if (file_part == "." || file_part == "..")
      continue;

    std::string name = path;
    name.push_back('/');
    name.append(file_part);

    // Don't allow the name to be too long which can get clipped.
    constexpr size_t kMaxName = 128;
    if (name.size() > kMaxName + 3) {  // 3 for ellipses
      std::string clipped = name.substr(name.size() - kMaxName);
      name = "..." + clipped;
    }

    fbl::RefPtr<VnodeMinfs> cur;
    if (zx_status_t status = minfs->VnodeGet(&cur, d->ino); status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot get the vnode for " << name << ": " << status;
      continue;
    }

    if (d->type == 8) {
      // File
      if (uint64_t size = cur->GetSize(); size >= kMinSizeToDump)
        FX_LOGS(WARNING) << " = " << name << ": " << size;
    } else if (d->type == 4) {
      // Directory
      DumpDir(minfs, *cur.get(), name);
    } else {
      // Minfs shouldn't have anything else.
      FX_LOGS(ERROR) << name << ": BAD TYPE";
    }
  }
}
#endif

}  // namespace

void SpaceDumper::SetMinfs(Minfs* minfs) {
  minfs_ = minfs;
}

void SpaceDumper::ClearMinfs() {
  minfs_ = nullptr;
}

void SpaceDumper::DumpFilesystem() {
#if defined(__Fuchsia__)
  if (!minfs_) {
    FX_LOGS(ERROR) << "Can't dump minfs, no global set.";
    return;
  }

  // Don't log too frequently in a row.
  static uint64_t last_dump_time = 0;
  const uint64_t min_interval = zx_ticks_per_second() * 30;
  uint64_t now = zx_ticks_get();
  if (last_dump_time && last_dump_time + min_interval > now) {
    FX_LOGS(WARNING) << "Skipping filesystem dump because it was recently completed.";
    return;
  }
  last_dump_time = now;

  fbl::RefPtr<VnodeMinfs> root;
  if (zx_status_t status = minfs_->VnodeGet(&root, kMinfsRootIno); status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot get the root filesystem: " << status;
    return;
  }

  FX_LOGS(WARNING) << "========== MINFS FILESYSTEM DUMP (size > " << kMinSizeToDump
                   << ") ==========";
  DumpDir(minfs_, *root, "/data");
  FX_LOGS(WARNING) << "========== DONE MINFS FILESYSTEM DUMP ==========";
#endif
}

}  // namespace minfs
