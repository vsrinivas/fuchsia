// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_DEBUG_DIRECTORY_H_
#define GARNET_BIN_APPMGR_DEBUG_DIRECTORY_H_

#include <fbl/string.h>
#include <fs/lazy-dir.h>
#include <lib/fxl/strings/string_view.h>
#include <zx/process.h>
#include <zx/thread.h>

namespace component {

class DebugDirectory : public fs::LazyDir {
 public:
  explicit DebugDirectory(const zx::process& process)
      : process_(process.get()) {}

  void GetContents(LazyEntryVector* out_vector) override;
  zx_status_t GetFile(fbl::RefPtr<Vnode>* out, uint64_t id,
                      fbl::String name) override;

 private:
  static constexpr size_t kMaxThreads = 2048;
  static constexpr uint64_t kAllId = 1;
  struct ThreadInfo {
    zx_koid_t koid;
    fbl::String name;
    zx::thread thread;
  };

  // Retrieves a list of ThreadInfos, one for each thread of the process.
  void GetThreads(fbl::Vector<ThreadInfo>* out);
  zx::process process_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_DEBUG_DIRECTORY_H_
