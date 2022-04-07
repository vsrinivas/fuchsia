// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_component_runner/fidl_async.dart' as fcrunner;
import 'package:fidl_fuchsia_data/fidl_async.dart' as fdata;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;
import 'package:fidl_fuchsia_component_test/fidl_async.dart' as ftest;

import 'package:fuchsia_vfs/vfs.dart' as vfs;

import 'local_component.dart';

class LocalComponentRunnerBuilder {
  final localComponents = <String, LocalComponent>{};

  LocalComponentRunnerBuilder();

  void registerLocalComponent(
    LocalComponent localComponent,
  ) {
    localComponents[localComponent.name] = localComponent;
  }

  fidl.InterfaceHandle<fcrunner.ComponentRunner> build() {
    final runner = LocalComponentRunner(localComponents);
    return runner.wrap();
  }
}

/// The [LocalComponentRunner] extends the ComponentRunner class and provides
/// concrete implementations for fidl defined methods.
class LocalComponentRunner extends fcrunner.ComponentRunner {
  final Map<String, LocalComponent> localComponents;

  final _binding = fcrunner.ComponentRunnerBinding();

  fidl.InterfaceHandle<fcrunner.ComponentRunner> wrap() => _binding.wrap(this);

  LocalComponentRunner(this.localComponents);

  @override
  Future<void> start(
    fcrunner.ComponentStartInfo startInfo,
    fidl.InterfaceRequest<fcrunner.ComponentController> controller,
  ) async {
    final numberedHandles = startInfo.numberedHandles;
    if (!(numberedHandles?.isEmpty ?? true)) {
      throw Exception('realm builder runner does not support numbered handles');
    }
    final program = startInfo.program;
    if (program == null) {
      throw Exception('program is missing from startInfo');
    }
    final namespace = startInfo.ns;
    if (namespace == null) {
      throw Exception('namespace is missing from startInfo');
    }
    final outgoingDir = startInfo.outgoingDir;
    if (outgoingDir == null) {
      throw Exception('outgoingDir is missing from startInfo');
    }
    final runtimeDirServerEnd = startInfo.runtimeDir;
    if (runtimeDirServerEnd == null) {
      throw Exception('runtimeDir is missing from startInfo');
    }

    final localComponentName = extractLocalComponentName(program);

    final localComponent = localComponents[localComponentName];
    if (localComponent == null) {
      throw Exception('no such local component: $localComponentName');
    }

    vfs.PseudoDir().open(
      fio.OpenFlags.rightReadable,
      fio.modeTypeDirectory,
      '.',
      fidl.InterfaceRequest<fio.Node>(runtimeDirServerEnd.passChannel()!),
    );

    await localComponent.run(
      controller,
      namespace,
      outgoingDir,
    );
  }
}

String extractLocalComponentName(fdata.Dictionary dict) {
  final entryValue = getDictionaryValue(dict, ftest.localComponentNameKey);
  if (entryValue == null) {
    throw Exception('program section is missing component name');
  }
  final s = entryValue.str;
  if (s == null) {
    throw Exception('malformed program section');
  }
  return s;
}

/// Returns a reference to the value corresponding to the key.
fdata.DictionaryValue? getDictionaryValue(fdata.Dictionary dict, String key) {
  final entries = dict.entries;
  if (entries != null) {
    for (final entry in entries) {
      if (entry.key == key) {
        return entry.value;
      }
    }
  }
  return null;
}
