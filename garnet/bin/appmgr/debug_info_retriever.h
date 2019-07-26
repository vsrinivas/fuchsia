// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_DEBUG_INFO_RETRIEVER_H_
#define GARNET_BIN_APPMGR_DEBUG_INFO_RETRIEVER_H_

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <inspector/inspector.h>
#include <lib/fit/function.h>
#include "src/lib/files/file.h"
#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace component {

struct DsoListWrapper {
  DsoListWrapper(const zx::process& process) { info = inspector_dso_fetch_list(process.get()); }
  ~DsoListWrapper() {
    if (info) {
      inspector_dso_free_list(info);
      info = nullptr;
    }
  }
  inspector_dsoinfo_t* info = nullptr;
};

class DebugInfoRetriever {
 public:
  // Retrieve stack traces for threads in the given process.
  // If thread_ids is not null, it must be an array of thread object ids of size
  // num to output. If thread_ids is null, all threads in the process will be
  // fetched and output.
  static fbl::String GetInfo(const zx::process* process, zx_koid_t* thread_ids = nullptr,
                             size_t num = 0);

 private:
  static constexpr int kMaxThreads = 1024;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_DEBUG_INFO_RETRIEVER_H_
