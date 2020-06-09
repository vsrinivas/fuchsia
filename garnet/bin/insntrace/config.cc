// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/insntrace/config.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "garnet/bin/insntrace/control.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace insntrace {

constexpr char IptConfig::kDefaultOutputPathPrefix[];

IptConfig::IptConfig()
    : mode(kDefaultMode),
      num_cpus(zx_system_get_num_cpus()),
      max_threads(kDefaultMaxThreads),
      num_chunks(kDefaultNumChunks),
      chunk_order(kDefaultChunkOrder),
      is_circular(kDefaultIsCircular),
      branch(true),
      cr3_match(0),
      cr3_match_set(false),
      cyc(false),
      cyc_thresh(0),
      mtc(false),
      mtc_freq(0),
      psb_freq(0),
      os(true),
      user(true),
      retc(true),
      tsc(true),
      output_path_prefix(kDefaultOutputPathPrefix) {
  addr[0] = AddrFilter::kOff;
  addr[1] = AddrFilter::kOff;
}

uint64_t IptConfig::CtlMsr() const {
  uint64_t msr = 0;

  // For documentation of the fields see the description of the IA32_RTIT_CTL
  // MSR in chapter 36 "Intel Processor Trace" of Intel Volume 3.

  if (cyc)
    msr |= 1 << 1;
  if (os)
    msr |= 1 << 2;
  if (user)
    msr |= 1 << 3;
  if (cr3_match)
    msr |= 1 << 7;
  if (mtc)
    msr |= 1 << 9;
  if (tsc)
    msr |= 1 << 10;
  if (!retc)
    msr |= 1 << 11;
  if (branch)
    msr |= 1 << 13;
  msr |= (mtc_freq & 15) << 14;
  msr |= (cyc_thresh & 15) << 19;
  msr |= (psb_freq & 15) << 24;
  msr |= (uint64_t)addr[0] << 32;
  msr |= (uint64_t)addr[1] << 36;

  return msr;
}

uint64_t IptConfig::AddrBegin(unsigned i) const {
  FX_DCHECK(i < std::size(addr_range));
  return addr_range[i].begin;
}

uint64_t IptConfig::AddrEnd(unsigned i) const {
  FX_DCHECK(i < std::size(addr_range));
  return addr_range[i].end;
}

}  // namespace insntrace
