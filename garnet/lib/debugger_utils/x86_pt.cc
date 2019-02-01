// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "x86_pt.h"

#include <cpuid.h>
#include <atomic>
#include <mutex>

#include "x86_cpuid.h"

namespace debugger_utils {

/* Trick to get a 1 of the right size */
#define ONE(x) (1 + ((x) - (x)))
#define BIT(x, bit) ((x) & (ONE(x) << (bit)))
#define BITS_SHIFT(x, high, low) \
  (((x) >> (low)) & ((ONE(x) << ((high) - (low) + 1)) - 1))

static X86ProcessorTraceFeatures pt_features;  // TODO(dje): guard annotation
static bool initialized = false;               // TODO(dje): guard annotation
static std::mutex cpuid_mutex;

bool X86HaveProcessorTrace() {
  const X86ProcessorTraceFeatures* ptf = X86GetProcessorTraceFeatures();
  return ptf->have_pt;
}

const X86ProcessorTraceFeatures* X86GetProcessorTraceFeatures() {
  std::lock_guard<std::mutex> lock(cpuid_mutex);

  X86ProcessorTraceFeatures* pt = &pt_features;

  if (initialized)
    return pt;

  memset(pt, 0, sizeof(*pt));
  initialized = true;

  if (!x86_feature_test(X86_FEATURE_PT))
    return pt;

  unsigned a, b, c, d;
  unsigned max_leaf = __get_cpuid_max(0, nullptr);

  pt->have_pt = true;

  __cpuid_count(0x14, 0, a, b, c, d);
  if (BIT(b, 2))
    pt->addr_cfg_max = 2;
  if (BIT(b, 1) && a >= 1) {
    unsigned a1, b1, c1, d1;
    __cpuid_count(0x14, 1, a1, b1, c1, d1);
    pt->mtc_freq_mask = (a1 >> 16) & 0xffff;
    pt->cycle_thresh_mask = b1 & 0xffff;
    pt->psb_freq_mask = (b1 >> 16) & 0xffff;
    pt->num_addr_ranges = a1 & 0x3;
  }

  pt->cr3_filtering = !!BIT(b, 0);
  pt->cycle_accurate_mode = !!BIT(b, 1);
  pt->ip_filtering = !!BIT(b, 2);
  pt->mtc = !!BIT(b, 3);
  pt->ptwrite = !!BIT(b, 4);
  pt->power_events = !!BIT(b, 5);

  pt->supports_filter_ranges = pt->addr_cfg_max >= 1;
  pt->supports_stop_ranges = pt->addr_cfg_max >= 2;

  pt->to_pa = !!BIT(c, 0);
  pt->multiple_to_pa_entries = !!BIT(c, 1);
  pt->single_range = !!BIT(c, 2);
  pt->trace_transport_output = !!BIT(c, 3);
  pt->payloads_are_lip = !!BIT(c, 31);

  if (max_leaf >= 0x15) {
    unsigned a1 = 0, b1 = 0, c1 = 0, d1 = 0;
    __cpuid(0x15, a1, b1, c1, d1);
    pt->tsc_ratio_den = a1;
    pt->tsc_ratio_num = b1;
  }

  return pt;
}

}  // namespace debugger_utils
