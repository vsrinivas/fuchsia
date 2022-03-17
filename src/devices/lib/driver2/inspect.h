// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_INSPECT_H_
#define SRC_DEVICES_LIB_DRIVER2_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <memory>

#include <src/lib/storage/vfs/cpp/synchronous_vfs.h>

namespace driver {

class ExposedInspector {
 public:
  ExposedInspector(const ExposedInspector&) = delete;
  ExposedInspector& operator=(const ExposedInspector&) = delete;

  ExposedInspector(ExposedInspector&&) noexcept = default;
  ExposedInspector& operator=(ExposedInspector&&) noexcept = default;

  static zx::status<ExposedInspector> Create(async_dispatcher_t* dispatcher,
                                             const inspect::Inspector& inspector,
                                             component::OutgoingDirectory& outgoing_directory);

 private:
  ExposedInspector(std::unique_ptr<fs::SynchronousVfs> vfs, zx::vmo vmo);

  zx::vmo vmo_;
  // |vfs_| has to be wrapped in a pointer because it's neither copy-able
  // nor move-able.
  std::unique_ptr<fs::SynchronousVfs> vfs_ = nullptr;
};

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_INSPECT_H_
