// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_LIB_PERFMON_CONTROLLER_IMPL_H_
#define SRC_PERFORMANCE_LIB_PERFMON_CONTROLLER_IMPL_H_

#include <fuchsia/perfmon/cpu/cpp/fidl.h>
#include <lib/zx/status.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/performance/lib/perfmon/controller.h"

namespace perfmon::internal {

using ::fuchsia::perfmon::cpu::ControllerSyncPtr;

class ControllerImpl final : public Controller {
 public:
  ControllerImpl(ControllerSyncPtr controller_ptr, uint32_t num_traces,
                 uint32_t buffer_size_in_pages, Config config);
  ~ControllerImpl() override;

  zx::status<> Start() override;
  // It is ok to call this while stopped.
  zx::status<> Stop() override;

  bool started() const override { return started_; }

  uint32_t num_traces() const override { return num_traces_; }

  const Config& config() const override { return config_; }

  zx::status<zx::vmo> GetBufferHandle(const std::string& name, uint32_t trace_num) override;

  zx::status<std::unique_ptr<Reader>> GetReader() override;

 private:
  zx::status<> Stage();
  zx::status<> Terminate();
  zx::status<> Reset();

  ControllerSyncPtr controller_ptr_;
  // The number of traces we will collect (== #cpus for now).
  uint32_t num_traces_;
  // This is the actual buffer size we use, in pages.
  const uint32_t buffer_size_in_pages_;
  const Config config_;

  // Set to true by |Start()|, false by |Stop()|.
  bool started_ = false;

  fxl::WeakPtrFactory<Controller> weak_ptr_factory_;
};

}  // namespace perfmon::internal

#endif  // SRC_PERFORMANCE_LIB_PERFMON_CONTROLLER_IMPL_H_
