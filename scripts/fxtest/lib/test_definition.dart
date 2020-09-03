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
  final String maxLogSeverity;
  final String parallel;

  String hash;

  final List<TestEnvironment> testEnvironments;

  TestDefinition({
    @required this.buildDir,
    @required this.name,
    @required this.os,
    this.packageUrl,
    this.cpu,
    this.command,
    this.runtimeDeps,
    this.label,
    this.path,
    this.maxLogSeverity,
    this.parallel,
    this.testEnvironments = const [],
  });

  factory TestDefinition.fromJson(
    Map<String, dynamic> data, {
    @required String buildDir,
  }) {
    Map<String, dynamic> testDetails = data['test'] ?? {};
    List<TestEnvironment> testEnvironments = (data['environments'] ?? [])
            .map((dynamic _data) => TestEnvironment.fromJson(_data))
            .cast<TestEnvironment>()
            .toList() ??
        [];
    Map<dynamic, dynamic> logSettings = testDetails['log_settings'] ?? {};
    return TestDefinition(
      buildDir: buildDir,
      command: List<String>.from(testDetails['command'] ?? []),
      cpu: testDetails['cpu'] ?? '',
      runtimeDeps: testDetails['runtime_deps'] ?? '',
      label: testDetails['label'] ?? '',
      name: testDetails['name'] ?? '',
      os: testDetails['os'] ?? '',
      packageUrl: testDetails['package_url'] == null
          ? null
          : PackageUrl.fromString(testDetails['package_url']),
      path: testDetails['path'] ?? '',
      maxLogSeverity: logSettings['max_severity'],
      parallel: testDetails['parallel']?.toString(),
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
  max_log_severity: $maxLogSeverity
  parallel: $parallel
/>''';

  TestType get testType {
    if (isE2E) {
      return TestType.e2e;
    } else if (command != null && command.isNotEmpty) {
      // `command` must be checked before `host`, because `command` is a subset
      // of all `host` tests
      return TestType.command;
    } else if (packageUrl != null) {
      // The order of `component` / `suite` does not currently matter

      // .cmx tests are considered components
      if (packageUrl.fullComponentName.endsWith('.cmx')) {
        return TestType.component;
        // .cm tests are considered suites
      } else if (packageUrl.fullComponentName.endsWith('.cm')) {
        return TestType.suite;
      }
      // Package Urls must end with either ".cmx" or ".cm"
      throw MalformedFuchsiaUrlException(packageUrl.toString());
    } else if (path != null && path.isNotEmpty) {
      // As per above, `host` must be checked after `command`

      // Tests with a path must be host tests. All Fuchsia tests *must* be
      // component tests, which means these are a legacy configuration which is
      // unsupported by `fx test`.
      if (os == 'fuchsia') {
        return TestType.unsupportedDeviceTest;
      } else {
        return TestType.host;
      }
    }
    return TestType.unsupported;
  }

  bool get isUnsupported => unsupportedTestTypes.contains(testType);

  PackageUrl get decoratedPackageUrl {
    if (packageUrl == null || hash == null) {
      return packageUrl;
    }
    return PackageUrl.copyWithHash(other: packageUrl, hash: hash);
  }

  ExecutionHandle createExecutionHandle() {
    switch (testType) {
      case TestType.component:
        return ExecutionHandle.component(decoratedPackageUrl.toString(), os);
      case TestType.suite:
        List<String> flags = [];
        if (parallel != null) {
          flags.addAll(['--parallel', parallel]);
        }
        return ExecutionHandle.suite(decoratedPackageUrl.toString(), os,
            flags: flags);
      case TestType.command:
        return ExecutionHandle.command(command.join(' '), os);
      case TestType.host:
        return ExecutionHandle.host(fullPath, os);
      case TestType.e2e:
        return ExecutionHandle.e2e([path, ...command].join(' '), os);
      case TestType.unsupported:
        return ExecutionHandle.unsupported();
      default:
        return ExecutionHandle.unsupportedDeviceTest(path);
    }
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
