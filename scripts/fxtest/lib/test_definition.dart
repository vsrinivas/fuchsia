// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

/// Concrete flag for an individual test, indicating how it should be executed.
///
/// Note that [unsupported] is included as a bucket for tests we have failed
/// to account for. It is not an acceptable place for tests to end up. Should
/// any tests find their way here, an exception will be raised that will halt
/// test execution entirely (but which can be silenced with a flag).
enum TestType { command, component, host, suite, unsupported }

class ExecutionHandle {
  final String handle;
  final TestType testType;
  ExecutionHandle(this.handle, {this.testType});
  ExecutionHandle.command(this.handle) : testType = TestType.command;
  ExecutionHandle.component(this.handle) : testType = TestType.component;
  ExecutionHandle.suite(this.handle) : testType = TestType.suite;
  ExecutionHandle.host(this.handle) : testType = TestType.host;
  ExecutionHandle.unsupported()
      : handle = '',
        testType = TestType.unsupported;

  bool get isUnsupported => testType == TestType.unsupported;
}

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
  final String packageUrl;
  final String name;
  final String os;

  final List<String> deviceTypes;
  final List<String> tags;

  ExecutionHandle _executionHandle;
  PackageUrl _parsedUrl;

  TestDefinition({
    @required this.buildDir,
    this.cpu,
    this.command,
    this.depsFile,
    this.deviceTypes,
    this.label,
    this.name,
    this.os,
    this.packageUrl,
    this.path,
    this.tags,
  });

  factory TestDefinition.fromJson({
    Map<String, dynamic> data,
    String buildDir,
  }) {
    Map<String, dynamic> testDetails = data['test'] ?? {};
    return TestDefinition(
      buildDir: buildDir,
      command: List<String>.from(testDetails['command'] ?? []),
      cpu: testDetails['cpu'] ?? '',
      depsFile: testDetails['deps_file'] ?? '',
      label: testDetails['label'] ?? '',
      name: testDetails['name'] ?? '',
      os: testDetails['os'] ?? '',
      packageUrl: testDetails['package_url'] ?? '',
      path: testDetails['path'] ?? '',
    );
  }

  @override
  String toString() => '''<TestDefinition
  cpu: $cpu
  command: ${command.join(" ")}
  deps_file: $depsFile
  label: $label
  package_url: $packageUrl
  path: $path
  name: $name
  os: $os
/>''';

  ExecutionHandle get executionHandle {
    return _executionHandle ??= _createExecutionHandle();
  }

  ExecutionHandle _createExecutionHandle() {
    // `command` must be checked before `host`, because `command` is a subset
    // of all `host` tests
    if (command != null && command.isNotEmpty) {
      return ExecutionHandle.command(command.join(' '));

      // The order of `component` / `suite` does not currently matter
    } else if (packageUrl != '' && packageUrl != null) {
      if (packageUrl.endsWith('.cmx')) {
        return ExecutionHandle.component(packageUrl);
      } else if (packageUrl.endsWith('.cm')) {
        return ExecutionHandle.suite(packageUrl);
      }
      throw MalformedFuchsiaUrlException(packageUrl);

      // As per above, `host` must be checked after `command`
    } else if (path != '' && path != null) {
      return ExecutionHandle.host(fullPath);
    }
    return ExecutionHandle.unsupported();
  }

  /// Destructured version of [packageUrl] - useful when you need a specific
  /// chunk of the [packageUrl], most commonly, the embedded [packageName].
  PackageUrl get parsedUrl {
    if (packageUrl == '' || packageUrl == null) {
      return PackageUrl.none();
    }
    return _parsedUrl ??= PackageUrl.fromString(packageUrl);
  }

  String get fullPath => p.join(buildDir, path);
}

class PackageUrl {
  final String host;
  final String packageName;
  final String packageVariant;
  final String hash;
  final String resourcePath;
  PackageUrl({
    this.host,
    this.packageName,
    this.packageVariant,
    this.hash,
    this.resourcePath,
  });

  PackageUrl.none()
      : host = null,
        hash = null,
        packageName = null,
        packageVariant = null,
        resourcePath = null;

  /// Breaks out a canonical Fuchsia URL into its constituent parts.
  ///
  /// Parses something like
  /// `fuchsia-pkg://host/package_name/variant?hash=1234#PATH` into:
  ///
  /// ```dart
  /// PackageUrl(
  ///   'host': 'host',
  ///   'packageName': 'package_name',
  ///   'packageVariant': 'variant',
  ///   'hash': '1234',
  ///   'resourcePath': 'PATH',
  /// );
  /// ```
  factory PackageUrl.fromString(String packageUrl) {
    Uri parsedUri = Uri.parse(packageUrl);

    if (parsedUri.scheme != 'fuchsia-pkg') {
      throw MalformedFuchsiaUrlException(packageUrl);
    }

    return PackageUrl(
      host: parsedUri.host,
      packageName:
          parsedUri.pathSegments.isNotEmpty ? parsedUri.pathSegments[0] : null,
      packageVariant:
          parsedUri.pathSegments.length > 1 ? parsedUri.pathSegments[1] : null,
      hash: parsedUri.queryParameters['hash'],
      resourcePath: parsedUri.fragment,
    );
  }
}
