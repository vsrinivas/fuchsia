// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
#define GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_

#include <fbl/string.h>
#include <fs/lazy-dir.h>
#include <lib/component/cpp/exposed_object.h>
#include <lib/fxl/strings/string_view.h>
#include <zx/process.h>
#include <zx/thread.h>

namespace component {

class SystemObjectsDirectory : public component::ExposedObject {
 public:
  explicit SystemObjectsDirectory(zx::process process)
      : ExposedObject("system_objects"),
        process_(std::move(process)),
        threads_(std::make_unique<ProcessThreads>(&process_)) {
    add_child(threads_.get());
  }

 private:
  class ProcessThreads : public component::ExposedObject {
   public:
    ProcessThreads(const zx::process* process);

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

    const zx::process* process_;
  };

  zx::process process_;
  std::unique_ptr<ProcessThreads> threads_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
