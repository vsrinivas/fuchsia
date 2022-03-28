// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_component_runner/fidl_async.dart' as fcrunner;

/// NOT YET IMPLEMENTED.
/// The [LocalComponentRunner] extends the ComponentRunner class and provides
/// concrete implementations for fidl defined methods.
class LocalComponentRunner extends fcrunner.ComponentRunner {
  final _binding = fcrunner.ComponentRunnerBinding();

  fidl.InterfaceHandle<fcrunner.ComponentRunner> wrap() => _binding.wrap(this);

  LocalComponentRunner();

  @override
  Future<void> start(
    fcrunner.ComponentStartInfo startInfo,
    fidl.InterfaceRequest<fcrunner.ComponentController> controller,
  ) async {
    if (!(startInfo.numberedHandles?.isEmpty ?? true)) {
      throw Exception('realm builder runner does not support numbered handles');
    }
    if (startInfo.program == null) {
      throw Exception('program is missing from startInfo');
    }
    if (startInfo.ns == null) {
      throw Exception('namespace is missing from startInfo');
    }
    if (startInfo.outgoingDir == null) {
      throw Exception('outgoingDir is missing from startInfo');
    }
    if (startInfo.runtimeDir == null) {
      throw Exception('runtimeDir is missing from startInfo');
    }
  }
}
