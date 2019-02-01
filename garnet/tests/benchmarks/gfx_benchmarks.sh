#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all gfx benchmarks for the Garnet layer. It is called by
# benchmarks.sh.

# Scenic performance tests.

# bench(): Helper function for running Scenic benchmarks in this file.
#
# Arguments:
#     $1         Label for benchmark.
#     $2         Command that is being benchmarked.
#     $3         Arguments to set_renderer_params
bench () {
  BENCHMARK=$1
  COMMAND=$2
  RENDERER_PARAMS=$3
  # Example of using runbench_exec:
  # runbench_exec
  #   "${OUT_DIR}/${BENCHMARK}.json"               # Output file path.
  #   "${RUN_SCENIC_BENCHMARK}"                    # Scenic benchmark runner, followed by
  #                                                #   its arguments.
  #   --out_dir "${OUT_DIR}"                       # Output directory.
  #   --out_file "${OUT_DIR}/${BENCHMARK}.json"    # Output file path.
  #   --benchmark_label "${BENCHMARK}"             # Label for benchmark.
  #   --cmd "test_binary"                          # Command that is being benchmarked.
  #   --unshadowed --clipping_disabled             # Arguments to set_renderer_params.
  runbench_exec "${OUT_DIR}/${BENCHMARK}.json"                           \
      "/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh"  \
      --out_dir "${OUT_DIR}"                                             \
      --out_file "${OUT_DIR}/${BENCHMARK}.json"                          \
      --benchmark_label "${BENCHMARK}"                                   \
      --cmd "${COMMAND}"                                                 \
      ${RENDERER_PARAMS}
}

#
# hello_scenic
#
hello_scenic="fuchsia-pkg://fuchsia.com/hello_scenic#meta/hello_scenic.cmx"
bench "fuchsia.scenic.hello_scenic" "$hello_scenic" ""

#
# image_grid_cpp
#
set_root_view="fuchsia-pkg://fuchsia.com/set_root_view#meta/set_root_view.cmx"
image_grid_cpp="fuchsia-pkg://fuchsia.com/image_grid_cpp#meta/image_grid_cpp.cmx"

bench "fuchsia.scenic.image_grid_cpp_noclipping_noshadows" \
      "$set_root_view $image_grid_cpp" \
      "--unshadowed --clipping_disabled"

bench "fuchsia.scenic.image_grid_cpp_noshadows" \
      "$set_root_view $image_grid_cpp" \
      "--unshadowed --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_ssdo" \
      "$set_root_view $image_grid_cpp" \
      "--screen_space_shadows --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_shadow_map" \
      "$set_root_view $image_grid_cpp" \
      "--shadow_map --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_moment_shadow_map" \
      "$set_root_view $image_grid_cpp" \
      "--moment_shadow_map --clipping_enabled"

#
# image_grid_cpp x3
#

tile_view="fuchsia-pkg://fuchsia.com/tile_view#meta/tile_view.cmx"

IMAGE_GRID_CPP_X3_COMMAND="$set_root_view $tile_view $image_grid_cpp $image_grid_cpp $image_grid_cpp"
bench "fuchsia.scenic.image_grid_cpp_x3_noclipping_noshadows" \
      "${IMAGE_GRID_CPP_X3_COMMAND}" \
      "--unshadowed --clipping_disabled"

bench "fuchsia.scenic.image_grid_cpp_x3_noshadows" \
      "${IMAGE_GRID_CPP_X3_COMMAND}" \
      "--unshadowed --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_x3_ssdo" \
      "${IMAGE_GRID_CPP_X3_COMMAND}" \
      "--screen_space_shadows --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_x3_shadow_map" \
      "${IMAGE_GRID_CPP_X3_COMMAND}" \
      "--shadow_map --clipping_enabled"

bench "fuchsia.scenic.image_grid_cpp_x3_moment_shadow_map" \
      "${IMAGE_GRID_CPP_X3_COMMAND}" \
      "--moment_shadow_map --clipping_enabled"
