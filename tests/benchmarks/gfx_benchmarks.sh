#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all gfx benchmarks for the Garnet layer. It is called by
# benchmarks.sh.

# Scenic performance tests.
RUN_SCENIC_BENCHMARK="/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh"


# Arguments to runbench_exec for scenic benchmarks:
#
# runbench_exec
#   "${OUT_DIR}/${BENCHMARK}.json"    # Output file path.
#   "${RUN_SCENIC_BENCHMARK}"         # Scenic benchmark runner, followed by
#                                     #   its arguments.
#   "${OUT_DIR}"                      # Output directory.
#   "${OUT_DIR}/${BENCHMARK}.json"    # Output file path.
#   "${BENCHMARK}"                    # Label for benchmark.
#   "test_binary"                     # Binary that is being benchmarked.
#   --unshadowed --clipping_disabled  # Arguments.

# hello_scenic
BENCHMARK="scenic.hello_scenic_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "hello_scenic"

# image_grid_cpp
BENCHMARK="scenic.image_grid_cpp_noclipping_noshadows_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "set_root_view image_grid_cpp" --unshadowed --clipping_disabled

BENCHMARK="scenic.image_grid_cpp_noshadows_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "set_root_view image_grid_cpp" --unshadowed --clipping_enabled

BENCHMARK="scenic.image_grid_cpp_ssdo_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "set_root_view image_grid_cpp" --screen_space_shadows --clipping_enabled

BENCHMARK="scenic.image_grid_cpp_shadow_map_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "set_root_view image_grid_cpp" --shadow_map --clipping_enabled

BENCHMARK="scenic.image_grid_cpp_moment_shadow_map_benchmark"
runbench_exec "${OUT_DIR}/${BENCHMARK}.json" \
    "${RUN_SCENIC_BENCHMARK}"                \
    "${OUT_DIR}"                             \
    "${OUT_DIR}/${BENCHMARK}.json"           \
    "${BENCHMARK}"                           \
    "set_root_view image_grid_cpp" --moment_shadow_map --clipping_enabled
