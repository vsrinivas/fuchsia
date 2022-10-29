// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_MOUNT_H_
#define SRC_STORAGE_F2FS_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>

#include "src/storage/f2fs/bcache.h"

namespace f2fs {

constexpr uint32_t kOptMaxNum = 13;
constexpr uint32_t kOptBgGcOff = 0;
constexpr uint32_t kOptDisableRollForward = 1;
constexpr uint32_t kOptDiscard = 2;
constexpr uint32_t kOptNoHeap = 3;
constexpr uint32_t kOptNoUserXAttr = 4;
constexpr uint32_t kOptNoAcl = 5;
constexpr uint32_t kOptDisableExtIdentify = 6;
constexpr uint32_t kOptInlineXattr = 7;
constexpr uint32_t kOptInlineData = 8;
constexpr uint32_t kOptInlineDentry = 9;
constexpr uint32_t kOptForceLfs = 10;
constexpr uint32_t kOptReadOnly = 11;
constexpr uint32_t kOptActiveLogs = (kOptMaxNum - 1);

constexpr uint64_t kMountBgGcOff = (1 << kOptBgGcOff);
constexpr uint64_t kMountDisableRollForward = (1 << kOptDisableRollForward);
constexpr uint64_t kMountDiscard = (1 << kOptDiscard);
constexpr uint64_t kMountNoheap = (1 << kOptNoHeap);
constexpr uint64_t kMountNoXAttr = (1 << kOptNoUserXAttr);
constexpr uint64_t kMountNoAcl = (1 << kOptNoAcl);
constexpr uint64_t kMountDisableExtIdentify = (1 << kOptDisableExtIdentify);
constexpr uint64_t kMountInlineXattr = (1 << kOptInlineXattr);
constexpr uint64_t kMountInlineData = (1 << kOptInlineData);
constexpr uint64_t kMountInlineDentry = (1 << kOptInlineDentry);
constexpr uint64_t kMountForceLfs = (1 << kOptForceLfs);

struct MountOpt {
  std::string name;
  uint32_t value;
  bool configurable;
};

class MountOptions {
 public:
  MountOptions();
  MountOptions(const MountOptions &) = default;

  zx_status_t GetValue(uint32_t opt_id, uint32_t *out) const;
  uint32_t GetOptionID(std::string_view opt) const;
  zx_status_t SetValue(std::string_view opt, uint32_t value);
  std::string_view GetNameView(const uint32_t opt_id) {
    ZX_ASSERT(opt_id < kOptMaxNum);
    return opt_[opt_id].name;
  }

 private:
  MountOpt opt_[kOptMaxNum];
};

#ifdef __Fuchsia__
// Start the filesystem on the block device backed by |bcache|, and serve it on |root|. Blocks
// until the filesystem terminates.
zx::result<> Mount(const MountOptions &options, std::unique_ptr<f2fs::Bcache> bcache,
                   fidl::ServerEnd<fuchsia_io::Directory> root);

zx::result<> StartComponent(fidl::ServerEnd<fuchsia_io::Directory> root,
                            fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle);
#endif

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_MOUNT_H_
