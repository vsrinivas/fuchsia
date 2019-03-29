// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/tests/benchmarks/gfx_benchmarks.h"

#include <unistd.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

bool IsVulkanSupported() {
  std::vector<std::string> command = {
      // clang-format off
      "/pkgfs/packages/run/0/bin/run",
      "fuchsia-pkg://fuchsia.com/vulkan_is_supported#meta/vulkan_is_supported.cmx",
      // clang-format on
  };

  std::vector<const char*> raw_command;
  for (const auto& arg : command) {
    raw_command.push_back(arg.c_str());
  }
  raw_command.push_back(nullptr);

  int pipefd[2];
  int pipe_status = pipe(pipefd);
  FXL_CHECK(pipe_status == 0);

  std::vector<fdio_spawn_action_t> actions = {
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = pipefd[1], .target_fd = 1}}};

  zx::handle subprocess;
  zx_status_t status = fdio_spawn_etc(
      ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, raw_command[0],
      raw_command.data(), nullptr, actions.size(), actions.data(),
      subprocess.reset_and_get_address(), nullptr);
  FXL_CHECK(status == ZX_OK);

  zx_signals_t signals_observed = 0;
  status = subprocess.wait_one(ZX_TASK_TERMINATED, zx::time(ZX_TIME_INFINITE),
                               &signals_observed);
  FXL_CHECK(status == ZX_OK);
  zx_info_process_t proc_info;
  status = subprocess.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                               nullptr, nullptr);
  FXL_CHECK(status == ZX_OK);

  FXL_CHECK(proc_info.return_code == 0);
  int close_result = close(pipefd[1]);
  FXL_CHECK(close_result == 0);

  std::optional<char> first_char;
  char c;
  if (read(pipefd[0], &c, sizeof(c)) > 0) {
    first_char = c;
  }

  close_result = close(pipefd[0]);
  FXL_CHECK(close_result == 0);

  if (first_char && *first_char == '1') {
    return true;
  } else if (first_char && *first_char == '0') {
    return false;
  } else {
    FXL_LOG(ERROR) << "Failed to run `vulkan_is_supported`";
    exit(1);
  }
}

void AddGraphicsBenchmarks(benchmarking::BenchmarksRunner* benchmarks_runner) {
  FXL_DCHECK(benchmarks_runner != nullptr);

  // Scenic performance tests.
  struct BenchmarkParams {
    std::string benchmark;
    std::string command;
    std::string renderer_params;
  };

  const std::string kPresentView =
      "fuchsia-pkg://fuchsia.com/present_view#meta/present_view.cmx";
  const std::string kImageGridCpp =
      "fuchsia-pkg://fuchsia.com/image_grid_cpp#meta/image_grid_cpp.cmx";
  const std::string kTileView =
      "fuchsia-pkg://fuchsia.com/tile_view#meta/tile_view.cmx";

  auto JoinCommands = [](const std::vector<std::string>& strings) {
    return fxl::JoinStrings(strings, " ");
  };
  const std::string kImageGridCppCommand =
      JoinCommands({kPresentView, kImageGridCpp});
  const std::string kImageGridCppX3Command = JoinCommands(
      {kPresentView, kTileView, kImageGridCpp, kImageGridCpp, kImageGridCpp});

  // clang-format off
  std::vector<BenchmarkParams> benchmark_params_list = {
    //
    // hello_scenic
    //
    {"fuchsia.scenic.hello_scenic", "fuchsia-pkg://fuchsia.com/hello_scenic#meta/hello_scenic.cmx", ""},
    //
    // image_grid_cpp
    //
    {"fuchsia.scenic.image_grid_cpp_noclipping_noshadows", kImageGridCppCommand, "--unshadowed --clipping_disabled"},
    {"fuchsia.scenic.image_grid_cpp_noshadows", kImageGridCppCommand, "--unshadowed --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_ssdo", kImageGridCppCommand, "--screen_space_shadows --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_shadow_map", kImageGridCppCommand, "--shadow_map --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_moment_shadow_map", kImageGridCppCommand, "--moment_shadow_map --clipping_enabled"},
    //
    // image_grid_cpp x3
    //
    {"fuchsia.scenic.image_grid_cpp_x3_noclipping_noshadows", kImageGridCppX3Command, "--unshadowed --clipping_disabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_noshadows", kImageGridCppX3Command, "--unshadowed --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_ssdo", kImageGridCppX3Command, "--screen_space_shadows --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_shadow_map", kImageGridCppX3Command, "--shadow_map --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_moment_shadow_map", kImageGridCppX3Command, "--moment_shadow_map --clipping_enabled"},
  };
  // clang-format on

  for (const auto& benchmark_params : benchmark_params_list) {
    std::string out_file = benchmarks_runner->MakeTempFile();
    benchmarks_runner->AddCustomBenchmark(
        benchmark_params.benchmark,
        {"/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh",
         "--out_file", out_file, "--benchmark_label",
         benchmark_params.benchmark, "--cmd", benchmark_params.command,
         benchmark_params.renderer_params},
        out_file);
  }
}
