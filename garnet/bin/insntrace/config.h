// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_INSNTRACE_CONFIG_H_
#define GARNET_BIN_INSNTRACE_CONFIG_H_

#include <fuchsia/hardware/cpu/insntrace/cpp/fidl.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <string>

#include "src/lib/fxl/macros.h"

namespace insntrace {

using Mode = ::fuchsia::hardware::cpu::insntrace::Mode;

// The parameters controlling data collection.

struct IptConfig {
  enum class AddrFilter { kOff = 0, kEnable = 1, kStop = 2 };
  struct AddrRange {
    // "" if no ELF
    std::string elf;
    uint64_t begin, end;
  };

  static constexpr Mode kDefaultMode = Mode::CPU;
  static constexpr uint32_t kDefaultMaxThreads = 16;
  static constexpr size_t kDefaultNumChunks = 16;
  static constexpr size_t kDefaultChunkOrder = 2;  // 16kb
  static constexpr bool kDefaultIsCircular = false;
  static constexpr char kDefaultOutputPathPrefix[] = "/tmp/ptout";

  IptConfig();

  // Return the value to write to the CTL MSR.
  uint64_t CtlMsr() const;

  // Return values for the addr range MSRs.
  uint64_t AddrBegin(unsigned index) const;
  uint64_t AddrEnd(unsigned index) const;

  Mode mode;

  // The number of cpus on this system, as reported by
  // zx_system_get_num_cpus().
  uint32_t num_cpus;

  // When tracing threads, the max number of threads we can trace.
  uint32_t max_threads;

  // Details of the tracing buffer.
  size_t num_chunks;
  // The size of each chunk, in pages as a power of 2.
  size_t chunk_order;
  bool is_circular;

  // The various fields of IA32_RTIT_CTL MSR, and support MSRs.
  AddrFilter addr[2];
  AddrRange addr_range[2];
  bool branch;
  // zero if disabled
  uint64_t cr3_match;
  // True if cr3_match was specified on the command line.
  bool cr3_match_set;
  bool cyc;
  uint32_t cyc_thresh;
  bool mtc;
  uint32_t mtc_freq;
  uint32_t psb_freq;
  bool os, user;
  bool retc;
  bool tsc;

  // The path prefix of all of the output files.
  std::string output_path_prefix;
};

}  // namespace insntrace

#endif  // GARNET_BIN_INSNTRACE_CONFIG_H_
