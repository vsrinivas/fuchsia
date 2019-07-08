// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
#define GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_

#include <fbl/string.h>
#include <fs/lazy-dir.h>
#include <lib/inspect_deprecated/deprecated/exposed_object.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <src/lib/fxl/strings/string_view.h>

namespace component {

class SystemObjectsDirectory : public component::ExposedObject {
 public:
  explicit SystemObjectsDirectory(zx::process process);

 private:
  class ThreadsDirectory : public component::ExposedObject {
   public:
    explicit ThreadsDirectory(std::shared_ptr<zx::process> process);

   private:
    static constexpr uint64_t kAllId = 1;
    std::shared_ptr<zx::process> process_;
    fit::deferred_action<fit::closure> cleanup_;
  };

  class MemoryDirectory : public component::ExposedObject {
   public:
    explicit MemoryDirectory(std::shared_ptr<zx::process> process);

   private:
    std::shared_ptr<zx::process> process_;
    fit::deferred_action<fit::closure> cleanup_;
  };

  // TODO(CF-761): Refactor this to use dynamic VMO nodes.
  std::shared_ptr<zx::process> process_;
  std::unique_ptr<ThreadsDirectory> threads_;
  std::unique_ptr<MemoryDirectory> memory_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SYSTEM_OBJECTS_DIRECTORY_H_
