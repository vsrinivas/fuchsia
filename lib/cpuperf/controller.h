// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_CONTROL_H_
#define GARNET_LIB_CPUPERF_CONTROL_H_

#include <stdint.h>

#include <memory>

#include <zircon/device/cpu-trace/cpu-perf.h>

#include "garnet/lib/cpuperf/reader.h"
#include "lib/fxl/files/unique_fd.h"

namespace cpuperf {

class Controller {
 public:
  Controller(uint32_t buffer_size_in_mb, const cpuperf_config_t& config);
  ~Controller();

  bool Start();
  // It is ok to call this while stopped.
  void Stop();

  bool is_valid() const;
  bool started() const { return started_; }

  std::unique_ptr<Reader> GetReader();

 private:
  void Alloc();
  bool Stage();
  void Free();

  // If |!sample_mode_| then we ignore the provided buffer size, we don't
  // need it.
  const bool sample_mode_;
  // This is the actual buffer size we use, in bytes.
  const uint32_t buffer_size_;
  const cpuperf_config_t config_;
  fxl::UniqueFD fd_;
  bool alloc_;
  bool started_;
};

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_CONTROL_H_
