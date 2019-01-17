#!/boot/bin/sh

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Example script for running a basic benchmark

killall scenic.cmx; killall basemgr; killall root_presenter.cmx; killall set_root_view; killall tiles; killall tiles.cmx

# Run benchmark
/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh \
    --out_dir /data \
    --out_file /data/bench \
    --benchmark_label BasicLatencyBenchmark \
    --sleep_before_trace 3 \
    --flutter_app_names 'image_grid_flutter.cmx,spinning_cube.cmx' \
    --cmd 'tiles_ctl start;
           tiles_ctl add fuchsia-pkg://fuchsia.com/spinning_cube#meta/spinning_cube.cmx;
           tiles_ctl add fuchsia-pkg://fuchsia.com/image_grid_flutter#meta/image_grid_flutter.cmx;'
