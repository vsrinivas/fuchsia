// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_FIDL_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_FIDL_H_

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/test/debug/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <vector>

#include "common.h"
#include "data_processor.h"

namespace ftest_debug = fuchsia::test::debug;

using DataProcessorInitializer =
    fit::function<std::unique_ptr<AbstractDataProcessor>(fbl::unique_fd)>;
using OnDoneCallback = fit::closure;

/// This class is not thread safe.
class DataProcessorFidl : public ftest_debug::DebugDataProcessor {
 public:
  DataProcessorFidl(fidl::InterfaceRequest<ftest_debug::DebugDataProcessor> request,
                    OnDoneCallback callback, DataProcessorInitializer initializer,
                    async_dispatcher_t* dispatcher);
  ~DataProcessorFidl() = default;

  void SetDirectory(fidl::InterfaceHandle<fuchsia::io::Directory> directory) override;

  void AddDebugVmos(::std::vector<::fuchsia::test::debug::DebugVmo> vmos,
                    AddDebugVmosCallback callback) override;

  void Finish(FinishCallback callback) override;

 private:
  void TearDown(zx_status_t epitaph);

  fidl::Binding<ftest_debug::DebugDataProcessor> binding_;
  OnDoneCallback on_done_;
  DataProcessorInitializer processor_initializer_;
  std::unique_ptr<AbstractDataProcessor> data_processor_;
  async::WaitOnce wait_for_completion_;
  async_dispatcher_t* dispatcher_;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_DATA_PROCESSOR_FIDL_H_
