// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include <magenta/device/intel-pt.h>
#include <magenta/syscalls.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

#include "inferior-control/exception-port.h"
#include "inferior-control/process.h"
#include "inferior-control/server.h"
#include "inferior-control/thread.h"

namespace debugserver {

constexpr uint32_t kDefaultMode = IPT_MODE_CPUS;
constexpr uint32_t kDefaultMaxThreads = 16;
constexpr size_t kDefaultNumBuffers = 16;
constexpr size_t kDefaultBufferOrder = 2;  // 16kb
constexpr bool kDefaultIsCircular = false;
constexpr uint64_t kDefaultCtlConfig = (
  IPT_CTL_OS_ALLOWED | IPT_CTL_USER_ALLOWED |
  IPT_CTL_BRANCH_EN |
  IPT_CTL_TSC_EN);

// The parameters controlling data collection.

struct IptConfig {
  IptConfig()
    : mode(kDefaultMode),
      num_cpus(mx_system_get_num_cpus()),
      max_threads(kDefaultMaxThreads),
      num_buffers(kDefaultNumBuffers),
      buffer_order(kDefaultBufferOrder),
      is_circular(kDefaultIsCircular),
      ctl_config(kDefaultCtlConfig)
    { }
  // One of IPT_MODE_CPUS, IPT_MODE_THREADS.
  uint32_t mode;
  // The number of cpus on this system, as reported by mx_system_get_num_cpus().
  uint32_t num_cpus;
  // When tracing threads, the max number of threads we can trace.
  uint32_t max_threads;
  size_t num_buffers;
  size_t buffer_order;
  bool is_circular;
  uint64_t ctl_config;
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

  // IOLoop::Delegate overrides.
  void OnBytesRead(const ftl::StringView& bytes) override;
  void OnDisconnected() override;
  void OnIOError() override;

  // Process::Delegate overrides.
  void OnThreadStarting(Process* process,
                        Thread* thread,
                        const mx_exception_context_t& context) override;
  void OnThreadExiting(Process* process,
                       Thread* thread,
                       const mx_excp_type_t type,
                       const mx_exception_context_t& context) override;
  void OnProcessExit(Process* process,
                     const mx_excp_type_t type,
                     const mx_exception_context_t& context) override;
  void OnArchitecturalException(Process* process,
                                Thread* thread,
                                const mx_excp_type_t type,
                                const mx_exception_context_t& context) override;

  IptConfig config_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IptServer);
};

}  // namespace debugserver
