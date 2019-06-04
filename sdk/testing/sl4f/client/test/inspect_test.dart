// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const _iqueryFindCommand = 'iquery --find /hub';
const _iqueryRecursiveInspectCommandPrefix = 'iquery --format=json --recursive';

class MockProcess extends Mock implements Process {}

void main(List<String> args) {
  test('inspectComponentRoot returns json decoded stdout', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(_createSshProcessFactory(
        findCommandStdOut: 'one\n$componentName\nthree\n',
        inspectCommandContentsRoot: [
          {
            'contents': {'root': contentsRoot}
          },
        ],
        expectedInspectSuffix: componentName));

    final result = await inspect.inspectComponentRoot(componentName);

    expect(result, equals(contentsRoot));
  });

  test('inspectComponentRoot fails if multiple components with same name',
      () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(_createSshProcessFactory(
        findCommandStdOut: 'one\n$componentName\n$componentName\n',
        inspectCommandContentsRoot: [
          {
            'contents': {'root': contentsRoot}
          },
          {
            'contents': {'root': contentsRoot}
          }
        ],
        expectedInspectSuffix: '$componentName $componentName'));

    try {
      await inspect.inspectComponentRoot(componentName);

      fail('inspectComponentRoot didn\'t throw exception as was expected');
      // ignore: avoid_catching_errors
    } on Error {
      // Pass through for expected exception.
    }
  });

  _testRetry('inspectComponentRoot retries on first find failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      findCommandExitCode: [-1, 0]);

  _testRetry('inspectComponentRoot retries on second find failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      findCommandExitCode: [-1, -1, 0]);

  _testRetry('inspectComponentRoot retries on third find failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      findCommandExitCode: [-1, -1, -1, 0]);

  _testRetry('inspectComponentRoot fails on fourth find failure',
      shouldFailDueToFind: true,
      shouldFailDueToInspect: false,
      findCommandExitCode: [-1, -1, -1, -1]);

  _testRetry('inspectComponentRoot retries on first inspect failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      inspectCommandExitCode: [-1, 0]);

  _testRetry('inspectComponentRoot retries on second inspect failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      inspectCommandExitCode: [-1, -1, 0]);

  _testRetry('inspectComponentRoot retries on third inspect failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      inspectCommandExitCode: [-1, -1, -1, 0]);

  _testRetry('inspectComponentRoot fails on fourth inspect failure',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: true,
      inspectCommandExitCode: [-1, -1, -1, -1]);

  _testRetry('inspectComponentRoot should succeed dispite multiple failures',
      shouldFailDueToFind: false,
      shouldFailDueToInspect: false,
      findCommandExitCode: [-1, -1, -1, 0],
      inspectCommandExitCode: [-1, -1, -1, 0]);
}

void _testRetry(
  String name, {
  bool shouldFailDueToFind,
  bool shouldFailDueToInspect,
  List<int> findCommandExitCode = const <int>[0],
  List<int> inspectCommandExitCode = const <int>[0],
}) {
  test(name, () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    // Pass in a fake sleep generator, that records the sleep durations.
    final sleepDurations = <Duration>[];
    Future<dynamic> autoCompleteSleep(Duration delay) {
      sleepDurations.add(delay);
      return Future.value();
    }

    final inspect = Inspect(
      _createSshProcessFactory(
          findCommandStdOut: 'one\n$componentName\n',
          inspectCommandContentsRoot: [
            {
              'contents': {'root': contentsRoot}
            }
          ],
          expectedInspectSuffix: '$componentName',
          findCommandExitCode: findCommandExitCode,
          inspectCommandExitCode: inspectCommandExitCode),
      sleep: autoCompleteSleep,
    );

    // Failing due to find causes an exception.
    if (shouldFailDueToFind) {
      try {
        await inspect.inspectComponentRoot(componentName);

        fail('inspectComponentRoot didn\'t throw exception as was expected');
        // ignore: avoid_catching_errors
      } on Error {
        // Pass through for expected exception.
      }
    } else {
      final result = await inspect.inspectComponentRoot(componentName);

      // Failing due to inspect returns null.
      if (shouldFailDueToInspect) {
        expect(result, isNull);
      } else {
        expect(result, equals(contentsRoot));
      }
    }

    // Verify all sleep durations are multiples of two of each other.
    // Find sleeps happen before inspect sleeps.
    expect(
        sleepDurations.length,
        equals(findCommandExitCode.length -
            1 +
            inspectCommandExitCode.length -
            1));
    for (int i = 0; i < findCommandExitCode.length - 2; i++) {
      expect(sleepDurations[i + 1], equals(sleepDurations[i] * 2));
    }
    sleepDurations.removeRange(0, findCommandExitCode.length - 1);
    for (int i = 0; i < inspectCommandExitCode.length - 2; i++) {
      expect(sleepDurations[i + 1], equals(sleepDurations[i] * 2));
    }
  });
}

Function _createSshProcessFactory({
  String findCommandStdOut,
  String expectedInspectSuffix,
  dynamic inspectCommandContentsRoot,
  List<int> findCommandExitCode = const <int>[0],
  List<int> inspectCommandExitCode = const <int>[0],
}) {
  int findCommandCount = 0;
  int inspectCommandCount = 0;
  return (String command) async {
    final process = MockProcess();
    if (command.trim() == _iqueryFindCommand) {
      when(process.exitCode).thenAnswer((_) async {
        final result = findCommandExitCode[findCommandCount];
        findCommandCount++;
        return result;
      });
      when(process.stdout).thenAnswer(
          (_) => Stream.fromIterable([utf8.encode(findCommandStdOut)]));
      when(process.stderr).thenAnswer((_) => Stream.empty());
    } else if (command.trim() ==
        '$_iqueryRecursiveInspectCommandPrefix $expectedInspectSuffix') {
      when(process.exitCode).thenAnswer((_) async {
        final result = inspectCommandExitCode[inspectCommandCount];
        inspectCommandCount++;
        return result;
      });
      when(process.stdout).thenAnswer((_) => Stream.fromIterable(
          [utf8.encode(json.encode(inspectCommandContentsRoot))]));
      when(process.stderr).thenAnswer((_) => Stream.empty());
    } else {
      print('got unknown command $command');
      when(process.exitCode).thenAnswer((_) async => -1);
      when(process.stdout).thenAnswer((_) => Stream.empty());
      when(process.stderr).thenAnswer((_) => Stream.empty());
    }
    return process;
  };
}
