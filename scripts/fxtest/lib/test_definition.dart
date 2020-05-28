// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

/// Structured representation of a single entry from `//out/default/tests.json`.
///
/// Contains all the relevant (e.g., we care about here) data from a single
/// test. Also contains an instance of [ExecutionHandle] which is used at
/// test-execution time to reduce ambiguity around low level invocation details.
class TestDefinition {
  final String buildDir;
  final List<String> command;
  final String cpu;
  final String depsFile;
  final String path;
  final String label;
  final String name;
  final String os;
  final PackageUrl packageUrl;

  ExecutionHandle executionHandle;

  TestDefinition({
    @required this.buildDir,
    @required this.name,
    @required this.os,
    @required String fx,
    this.packageUrl,
    this.cpu,
    this.command,
    this.depsFile,
    this.label,
    this.path,
  }) {
    executionHandle = _createExecutionHandle(fx);
  }

  factory TestDefinition.fromJson(
    Map<String, dynamic> data, {
    @required String buildDir,
    @required String fx,
    PackageRepository packageRepository,
  }) {
    Map<String, dynamic> testDetails = data['test'] ?? {};

    return TestDefinition(
      buildDir: buildDir,
      command: List<String>.from(testDetails['command'] ?? []),
      cpu: testDetails['cpu'] ?? '',
      depsFile: testDetails['deps_file'] ?? '',
      fx: fx,
      label: testDetails['label'] ?? '',
      name: testDetails['name'] ?? '',
      os: testDetails['os'] ?? '',
      packageUrl: PackageRepository.decoratePackageUrlWithHash(
          packageRepository, testDetails['package_url']),
      path: testDetails['path'] ?? '',
    );
  }

  @override
  String toString() => '''<TestDefinition
  cpu: $cpu
  command: ${(command ?? []).join(" ")}
  deps_file: $depsFile
  label: $label
  package_url: ${packageUrl ?? ''}
  path: $path
  name: $name
  os: $os
/>''';

  ExecutionHandle _createExecutionHandle(String fxPath) {
    // `command` must be checked before `host`, because `command` is a subset
    // of all `host` tests
    if (command != null && command.isNotEmpty) {
      return ExecutionHandle.command(fxPath, command.join(' '), os);

      // The order of `component` / `suite` does not currently matter
    } else if (packageUrl != null) {
      // .cmx tests are considered components
      if (packageUrl.fullComponentName.endsWith('.cmx')) {
        return ExecutionHandle.component(fxPath, packageUrl.toString(), os);

        // .cm tests are considered suites
      } else if (packageUrl.fullComponentName.endsWith('.cm')) {
        return ExecutionHandle.suite(fxPath, packageUrl.toString(), os);
      }

      // Package Urls must end with either ".cmx" or ".cm"
      throw MalformedFuchsiaUrlException(packageUrl.toString());

      // As per above, `host` must be checked after `command`
    } else if (path != '' && path != null) {
      // Tests with a path must be host tests. All Fuchsia tests *must* be
      // component tests, which means these are a legacy configuration which is
      // unsupported by `fx test`.
      if (os == 'fuchsia') {
        return ExecutionHandle.unsupportedDeviceTest(path);
      }

      return ExecutionHandle.host(fxPath, fullPath, os);
    }
    return ExecutionHandle.unsupported();
  }

  String get fullPath => p.join(buildDir, path);
}
