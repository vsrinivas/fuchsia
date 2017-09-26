// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_READER_H_
#define GARNET_BIN_CPUPERF_PROVIDER_READER_H_

#include <zircon/device/intel-pm.h>
#include <zx/vmo.h>

#include "lib/fxl/macros.h"

namespace cpuperf_provider {

class Reader {
public:
  Reader(int fd);

  uint32_t num_cpus() const { return num_cpus_; }

  uint64_t ticks_per_second() const { return ticks_per_second_; }

  bool ReadState(zx_x86_ipm_state_t* state);

  bool ReadPerfConfig(zx_x86_ipm_perf_config_t* config);

  bool ReadNextRecord(uint32_t* cpu, zx_x86_ipm_counters_t* counters);
  bool ReadNextRecord(uint32_t* cpu, zx_x86_ipm_sample_record_t* record);

private:
  int fd_; // borrowed
  const uint32_t num_cpus_;
  uint32_t current_cpu_ = 0;

  // Note: The following are only used in sampling mode.
  zx::vmo current_vmo_;
  uint64_t next_record_ = 0;
  uint64_t capture_end_ = 0;

  // Reading of one trace can span multiple cpus, and the ticks-per-second
  // value comes from each cpu's trace. Generally it's all the same value,
  // but there is no uber record to specify that, and ultimately the value
  // will vary anyway. But we don't want each record to encode its own
  // value, so keep track of the current value here.
  uint64_t ticks_per_second_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
};

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_READER_H_
