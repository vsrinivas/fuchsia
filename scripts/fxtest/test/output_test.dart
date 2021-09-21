// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:test/test.dart';
import 'fake_fx_env.dart';
import 'helpers.dart';

typedef TestBundleBuilder = TestBundle Function(TestDefinition, [double]);
typedef EventEmitter = void Function(TestEvent);

class EmptyTestManifestReader extends TestsManifestReader {
  @override
  Future<List<TestDefinition>> loadTestsJson(
          {@required String buildDir,
          @required String fxLocation,
          @required String manifestFileName,
          bool usePackageHash = true}) async =>
      <TestDefinition>[];

  @override
  ParsedManifest aggregateTests({
    @required TestBundleBuilder testBundleBuilder,
    @required List<TestDefinition> testDefinitions,
    @required EventEmitter eventEmitter,
    @required TestsConfig testsConfig,
    Comparer comparer,
    MatchLength matchLength = MatchLength.partial,
  }) =>
      ParsedManifest(testDefinitions: [], testBundles: []);

  @override
  void reportOnTestBundles({
    @required ParsedManifest parsedManifest,
    @required TestsConfig testsConfig,
    @required EventEmitter eventEmitter,
    @required String userFriendlyBuildDir,
  }) =>
      null;
}

void main() {
  OutputBuffer buffer;
  OutputFormatter outputFormatter;
  StreamController<TestEvent> streamController;

  var testDefinition = TestDefinition.fromJson(
    {
      'environments': [],
      'test': {
        'cpu': 'x64',
        'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
        'name': 'lib_tests',
        'os': 'linux',
        'path': 'host_x64/lib_tests',
        'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
      }
    },
    buildDir: '/whatever',
  );

  setUp(() async {
    streamController = StreamController<TestEvent>();
    buffer = OutputBuffer.locMemIO();
  });

  tearDown(() {
    streamController.close();
  });

  group('output is collected correctly', () {
    test('for a passing test', () async {
      final testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      outputFormatter = OutputFormatter.fromConfig(
        testsConfig,
        buffer: buffer,
      );
      var runner = ScriptedTestRunner(scriptedOutput: [
        Output('test'),
        Output('test 2'),
        ErrOutput('test 3'),
      ]);
      // 0 is the default exit code if no other exit code was set.
      int commandExitCode = 0;
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        outputFormatter: outputFormatter,
        testRunnerBuilder: (TestsConfig config) => runner,
        exitCodeSetter: (e) => commandExitCode = e,
      );
      // Use the command's builder so we test how things are actually wired up,
      // instead of duplicating the wiring-up here
      var bundle = cmd.testBundleBuilder(testDefinition);
      await cmd.runTests([bundle]);
      expect(buffer.content, hasLength(1));
      // Output that the test passed. Note that the _test is running_ content
      // has been overwritten.
      expect(buffer.content.first, contains('✅'));
      expect(buffer.content.first, contains('/whatever/host_x64/lib_tests'));
      expect(commandExitCode, 0);
    });

    test('for a failing tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      outputFormatter = OutputFormatter.fromConfig(
        testsConfig,
        buffer: buffer,
      );
      var runner = ScriptedTestRunner(exitCode: 2, scriptedOutput: [
        Output('test 1'),
        Output('test 2'),
        ErrOutput('test 3'),
      ]);
      // 0 is the default exit code if no other exit code was set.
      int commandExitCode = 0;
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        outputFormatter: outputFormatter,
        testRunnerBuilder: (TestsConfig config) => runner,
        exitCodeSetter: (e) => commandExitCode = e,
      );
      // Use the command's builder so we test how things are actually wired up,
      // instead of duplicating the wiring-up here
      var bundle = cmd.testBundleBuilder(testDefinition);
      await cmd.runTests([bundle]);
      var allOutput = buffer.content.join('\n');
      // All test output appears
      expect(allOutput, contains('test 1'));
      expect(allOutput, contains('test 2'));
      expect(allOutput, contains('test 3'));
      // Output that the test failed. Note that the _test is running_ content
      // has been overwritten.
      expect(allOutput, contains('❌'));
      expect(allOutput, contains('/whatever/host_x64/lib_tests'));
      expect(commandExitCode, 2);
    });
  });

  group('exit code', () {
    test('should be non-zero when no tests are found', () async {
      final testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      int commandExitCode = 0;
      final cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        exitCodeSetter: (e) => commandExitCode = e,
        outputFormatter: OutputFormatter.fromConfig(
          testsConfig,
          buffer: OutputBuffer.locMemIO(),
        ),
        testRunnerBuilder: (TestsConfig config) => FakeTestRunner.passing(),
      );
      await cmd.runTestSuite(EmptyTestManifestReader());
      expect(commandExitCode, equals(noTestFoundExitCode));
    });
  });
}
