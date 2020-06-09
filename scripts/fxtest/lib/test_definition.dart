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
  final String runtimeDeps;
  final String path;
  final String label;
  final String name;
  final String os;
  final PackageUrl packageUrl;

  final List<TestEnvironment> testEnvironments;

  ExecutionHandle executionHandle;

  TestDefinition({
    @required this.buildDir,
    @required this.name,
    @required this.os,
    @required String fx,
    this.packageUrl,
    this.cpu,
    this.command,
    this.runtimeDeps,
    this.label,
    this.path,
    this.testEnvironments = const [],
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
    List<TestEnvironment> testEnvironments = (data['environments'] ?? [])
            .map((dynamic _data) => TestEnvironment.fromJson(_data))
            .cast<TestEnvironment>()
            .toList() ??
        [];
    return TestDefinition(
      buildDir: buildDir,
      command: List<String>.from(testDetails['command'] ?? []),
      cpu: testDetails['cpu'] ?? '',
      runtimeDeps: testDetails['runtime_deps'] ?? '',
      fx: fx,
      label: testDetails['label'] ?? '',
      name: testDetails['name'] ?? '',
      os: testDetails['os'] ?? '',
      packageUrl: PackageRepository.decoratePackageUrlWithHash(
          packageRepository, testDetails['package_url']),
      path: testDetails['path'] ?? '',
      testEnvironments: testEnvironments,
    );
  }

  @override
  String toString() => '''<TestDefinition
  cpu: $cpu
  command: ${(command ?? []).join(" ")}
  deps_file: $runtimeDeps
  label: $label
  package_url: ${packageUrl ?? ''}
  path: $path
  name: $name
  os: $os
/>''';

  ExecutionHandle _createExecutionHandle(String fxPath) {
    if (isE2E) {
      return ExecutionHandle.e2e(fxPath, [path, ...command].join(' '), os);
    }

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
    } else if (path != null && path.isNotEmpty) {
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

  /// End-to-end tests start on the host machine (designated by an [os] value
  /// of `linux`), but also require interaction with a physical device.
  bool get isE2E =>
      (os == null || os.toLowerCase() != 'fuchsia') && containsE2eEnvironments;

  bool get containsE2eEnvironments =>
      testEnvironments != null && testEnvironments.any((env) => env.isE2E);

  String get fullPath => p.join(buildDir, path);
}

/// Structured representation of an optionally populated `environments` key
/// inside individual test blocks within `tests.json`.
///
/// `TestEnvironment` separates the task of making sense of that data from the
/// core task of parsing a test block's simpler key/value pairs.
///
/// `TestEnvironment` is a work-in-progress and does not exhaustively contain
/// every field that can exist in the `environments` list. In the future, if new
/// feature requests require the use of a field not yet handled, this is not an
/// unexpected shortcoming and you should feel confident adding it.
class TestEnvironment {
  static const hostOsValues = <String>{'linux', 'mac'};

  final bool isDefined;
  final String deviceDimension;
  final String os;
  TestEnvironment._({
    @required this.isDefined,
    @required this.deviceDimension,
    @required this.os,
  });

  factory TestEnvironment.fromJson(Map<String, dynamic> data) {
    if (data == null || data.isEmpty) return TestEnvironment.empty();
    return TestEnvironment._(
        isDefined: true,
        deviceDimension: getMapPath(data, ['dimensions', 'device_type']),
        os: getMapPath(data, ['dimensions', 'os']));
  }

  factory TestEnvironment.empty() => TestEnvironment._(
        isDefined: false,
        deviceDimension: null,
        os: null,
      );

  bool get requiresDevice => deviceDimension != null;
  bool get nonHostOs => os == null || !hostOsValues.contains(os.toLowerCase());

  bool get isE2E => isDefined == true && (requiresDevice || nonHostOs);

  @override
  String toString() =>
      '<TestEnvironment os: "$os", deviceType: "$deviceDimension" />';
}
