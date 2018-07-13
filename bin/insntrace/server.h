// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

#include <zircon/device/cpu-trace/intel-pt.h>
#include <zircon/syscalls.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

#include "garnet/lib/inferior_control/exception_port.h"
#include "garnet/lib/inferior_control/process.h"
#include "garnet/lib/inferior_control/server.h"
#include "garnet/lib/inferior_control/thread.h"

namespace debugserver {

// The parameters controlling data collection.

struct IptConfig {
  enum class AddrFilter { kOff = 0, kEnable = 1, kStop = 2 };
  struct AddrRange {
    // "" if no ELF
    std::string elf;
    uint64_t begin, end;
  };

  static constexpr uint32_t kDefaultMode = IPT_MODE_CPUS;
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

  // One of IPT_MODE_CPUS, IPT_MODE_THREADS.
  uint32_t mode;

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

// IptServer implements the main loop, which basically just waits until
// the inferior exits. The exception port thread does all the heavy lifting
// when tracing threads.
//
// NOTE: This class is generally not thread safe. Care must be taken when
// calling methods which modify the internal state of a IptServer instance.
class IptServer final : public Server {
 public:
  IptServer(const IptConfig& config);

  bool Run() override;

 private:
  bool StartInferior();
  bool DumpResults();

  // Process::Delegate overrides.
  void OnThreadStarting(Process* process, Thread* thread,
                        const zx_exception_context_t& context) override;
  void OnThreadExiting(Process* process, Thread* thread,
                       const zx_exception_context_t& context) override;
  void OnProcessExit(Process* process) override;
  void OnArchitecturalException(Process* process, Thread* thread,
                                const zx_excp_type_t type,
                                const zx_exception_context_t& context) override;
  void OnSyntheticException(Process* process, Thread* thread,
                            zx_excp_type_t type,
                            const zx_exception_context_t& context) override;

  IptConfig config_;

  FXL_DISALLOW_COPY_AND_ASSIGN(IptServer);
};

}  // namespace debugserver
