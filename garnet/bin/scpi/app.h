// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SCPI_APP_H_
#define GARNET_BIN_SCPI_APP_H_

#include <fcntl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <fuchsia/scpi/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/files/unique_fd.h"

namespace scpi {

class App : public fuchsia::scpi::SystemController {
 public:
  App();
  ~App() override;
  explicit App(std::unique_ptr<sys::ComponentContext> context);
  void GetDvfsInfo(uint32_t power_domain, GetDvfsInfoCallback callback) final;
  void GetSystemStatus(GetSystemStatusCallback callback) final;
  zx_status_t Start();

 private:
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  std::unique_ptr<llcpp::fuchsia::kernel::Stats::SyncClient> GetStatsService();
  bool ReadCpuStats();
  bool ReadMemStats();
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::scpi::SystemController> bindings_;
  zx::handle thermal_handle_;
  std::unique_ptr<llcpp::fuchsia::kernel::Stats::SyncClient> stats_;
  std::unique_ptr<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetCpuStatsResponse>>
      cpu_stats_buffer_;
  llcpp::fuchsia::kernel::CpuStats* cpu_stats_ = nullptr;
  std::unique_ptr<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetCpuStatsResponse>>
      last_cpu_stats_buffer_;
  llcpp::fuchsia::kernel::CpuStats* last_cpu_stats_ = nullptr;
  std::unique_ptr<fidl::Buffer<llcpp::fuchsia::kernel::Stats::GetMemoryStatsResponse>>
      mem_stats_buffer_;
  llcpp::fuchsia::kernel::MemoryStats* mem_stats_ = nullptr;
  size_t num_cores_;
};

}  // namespace scpi

#endif  // GARNET_BIN_SCPI_APP_H_
