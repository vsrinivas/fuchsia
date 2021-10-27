// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_MEMORY_PRESSURE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_MEMORY_PRESSURE_H_

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl_test_base.h>

#include "lib/fidl/cpp/interface_handle.h"
#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics::stubs {

using MemoryPressureBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::memorypressure, Provider);

class MemoryPressure : public MemoryPressureBase {
 public:
  explicit MemoryPressure(async_dispatcher_t* dispatcher);

  // fuchsia.memorypressure.Provider
  void RegisterWatcher(::fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) override;

  void ChangePressureLevel(fuchsia::memorypressure::Level level);

 private:
  async_dispatcher_t* dispatcher_;
  fuchsia::memorypressure::WatcherPtr watcher_;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_MEMORY_PRESSURE_H_
