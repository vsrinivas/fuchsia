// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdio>

#include "lib/fxl/logging.h"

namespace debugger_utils {

struct X86ProcessorTraceFeatures {
  bool have_pt;

  bool cr3_filtering;
  bool cycle_accurate_mode;
  bool ip_filtering;
  bool mtc;
  bool ptwrite;
  bool power_events;

  uint32_t addr_cfg_max;
  bool supports_filter_ranges;
  bool supports_stop_ranges;
  uint32_t num_addr_ranges;

  uint32_t mtc_freq_mask;
  uint32_t cycle_thresh_mask;
  uint32_t psb_freq_mask;

  bool to_pa;
  bool multiple_to_pa_entries;
  bool single_range;
  bool trace_transport_output;
  bool payloads_are_lip;

  uint32_t tsc_ratio_num, tsc_ratio_den;
};

bool X86HaveProcessorTrace();

// WARNING: Until the first call completes this is not thread safe.
const X86ProcessorTraceFeatures* X86GetProcessorTraceFeatures();

}  // namespace debugger_utils
