// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_CONTROLLER_IMPL_H_
#define GARNET_LIB_PERFMON_CONTROLLER_IMPL_H_

#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "garnet/lib/perfmon/controller.h"

namespace perfmon {
namespace internal {

class ControllerImpl final : public Controller {
 public:
  ControllerImpl(fxl::UniqueFD fd, uint32_t num_traces,
                 uint32_t buffer_size_in_pages, Config config);
  ~ControllerImpl() override;

  bool Start() override;
  // It is ok to call this while stopped.
  void Stop() override;

  bool started() const override { return started_; }

  uint32_t num_traces() const override { return num_traces_; }

  const Config& config() const override { return config_; }

  bool GetBufferHandle(const std::string& name, uint32_t trace_num,
                       zx::vmo* out_vmo) override;

  std::unique_ptr<Reader> GetReader() override;

 private:
  bool Stage();
  void Free();
  void Reset();

  fxl::UniqueFD fd_;
  // The number of traces we will collect (== #cpus for now).
  uint32_t num_traces_;
  // This is the actual buffer size we use, in pages.
  const uint32_t buffer_size_in_pages_;
  const Config config_;

  // Set to true by |Start()|, false by |Stop()|.
  bool started_ = false;

  fxl::WeakPtrFactory<Controller> weak_ptr_factory_;
};

}  // namespace internal
}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_CONTROLLER_IMPL_H_
