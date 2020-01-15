// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  void addTest(String benchmark, String command, String rendererParams) {
    test(benchmark, () async {
      final helper = await PerfTestHelper.make();
      final resultsFile = '/tmp/perf_results_$benchmark.json';
      final result = await helper.sl4fDriver.ssh.run(
          '/pkgfs/packages/scenic_benchmarks/0/bin/run_scenic_benchmark.sh'
          ' --out_file $resultsFile'
          ' --benchmark_label $benchmark --cmd "$command" "$rendererParams"');
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  }

  const String kPresentView =
      'fuchsia-pkg://fuchsia.com/present_view#meta/present_view.cmx';
  const String kImageGridCpp =
      'fuchsia-pkg://fuchsia.com/image_grid_cpp#meta/image_grid_cpp.cmx';
  const String kTileView =
      'fuchsia-pkg://fuchsia.com/tile_view#meta/tile_view.cmx';

  const String kImageGridCppCommand = '$kPresentView $kImageGridCpp';
  const String kImageGridCppX3Command =
      '$kPresentView $kTileView $kImageGridCpp $kImageGridCpp $kImageGridCpp';

  // hello_scenic
  //
  // Note: "hello_scenic" was renamed "standalone_app" at some point.  We
  // use its original name as the benchmark name so that it shows up on the
  // same dashboard graph.
  addTest('fuchsia.scenic.hello_scenic',
      'fuchsia-pkg://fuchsia.com/standalone_app#meta/standalone_app.cmx', '');

  // image_grid_cpp
  addTest('fuchsia.scenic.image_grid_cpp_noclipping_noshadows',
      kImageGridCppCommand, '--unshadowed --clipping_disabled');
  addTest('fuchsia.scenic.image_grid_cpp_noshadows', kImageGridCppCommand,
      '--unshadowed --clipping_enabled');
  addTest('fuchsia.scenic.image_grid_cpp_stencil_shadow_volume',
      kImageGridCppCommand, '--stencil_shadow_volume --clipping_enabled');

  // image_grid_cpp x3
  addTest('fuchsia.scenic.image_grid_cpp_x3_noclipping_noshadows',
      kImageGridCppX3Command, '--unshadowed --clipping_disabled');
  addTest('fuchsia.scenic.image_grid_cpp_x3_noshadows', kImageGridCppX3Command,
      '--unshadowed --clipping_enabled');
  addTest('fuchsia.scenic.image_grid_cpp_x3_stencil_shadow_volume',
      kImageGridCppX3Command, '--stencil_shadow_volume --clipping_enabled');
}
