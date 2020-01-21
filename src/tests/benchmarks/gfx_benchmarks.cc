// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/benchmarks/gfx_benchmarks.h"

#include <unistd.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"

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
    // Note: "hello_scenic" was renamed "standalone_app" at some point.  We use
    // its original name as the benchmark name so that it shows up on the same
    // dashboard graph.
    //
    {"fuchsia.scenic.hello_scenic", "fuchsia-pkg://fuchsia.com/standalone_app#meta/standalone_app.cmx", ""},
    //
    // image_grid_cpp
    //
    {"fuchsia.scenic.image_grid_cpp_noclipping_noshadows", kImageGridCppCommand, "--unshadowed --clipping_disabled"},
    {"fuchsia.scenic.image_grid_cpp_noshadows", kImageGridCppCommand, "--unshadowed --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_stencil_shadow_volume", kImageGridCppCommand, "--stencil_shadow_volume --clipping_enabled"},
    //
    // image_grid_cpp x3
    //
    {"fuchsia.scenic.image_grid_cpp_x3_noclipping_noshadows", kImageGridCppX3Command, "--unshadowed --clipping_disabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_noshadows", kImageGridCppX3Command, "--unshadowed --clipping_enabled"},
    {"fuchsia.scenic.image_grid_cpp_x3_stencil_shadow_volume", kImageGridCppX3Command, "--stencil_shadow_volume --clipping_enabled"},
  };
  // clang-format on

  for (const auto& benchmark_params : benchmark_params_list) {
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename("scenic");
    benchmarks_runner->AddCustomBenchmark(
        benchmark_params.benchmark,
        {"/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh",
         "--out_file", out_file, "--benchmark_label",
         benchmark_params.benchmark, "--cmd", benchmark_params.command,
         benchmark_params.renderer_params},
        out_file);
  }
}
