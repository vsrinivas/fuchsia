// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_F2FS_MOUNT_H_
#define THIRD_PARTY_F2FS_MOUNT_H_

namespace f2fs {

constexpr uint32_t kOptMaxNum = 11;
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

struct MountOpt {
  std::string name;
  uint32_t value;
  bool configurable;
};

class MountOptions {
 public:
  MountOptions();
  MountOptions(const MountOptions &) = default;

  zx_status_t GetValue(const uint32_t opt_id, uint32_t *out);
  uint32_t GetOptionID(const std::string_view &opt);
  zx_status_t SetValue(const std::string_view &opt, const uint32_t value);
  const std::string_view GetNameView(const uint32_t opt_id) {
    ZX_ASSERT(opt_id < kOptMaxNum);
    return opt_[opt_id].name;
  }

 private:
  MountOpt opt_[kOptMaxNum];
};

inline void ClearOpt(SbInfo *sbi, uint64_t option) { sbi->mount_opt &= ~option; }
inline void SetOpt(SbInfo *sbi, uint64_t option) { sbi->mount_opt |= option; }
inline bool TestOpt(SbInfo *sbi, uint64_t option) { return ((sbi->mount_opt & option) != 0); }
zx_status_t Mount(const MountOptions &options, std::unique_ptr<f2fs::Bcache> bc);

}  // namespace f2fs

#endif  // THIRD_PARTY_F2FS_MOUNT_H_
