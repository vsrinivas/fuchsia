// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:quiver/testing/async.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const _iqueryFindCommand = 'iquery --find /hub';
const _iqueryRecursiveInspectCommandPrefix = 'iquery --format=json --recursive';

void main(List<String> args) {
  test('inspectComponentRoot returns json decoded stdout', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(FakeSsh(
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

    final inspect = Inspect(FakeSsh(
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

  _testRetry('inspectComponentRoot should succeed despite multiple failures',
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

    final fakeSsh = FakeSsh(
        findCommandStdOut: 'one\n$componentName\n',
        inspectCommandContentsRoot: [
          {
            'contents': {'root': contentsRoot}
          }
        ],
        expectedInspectSuffix: '$componentName',
        findCommandExitCode: findCommandExitCode,
        inspectCommandExitCode: inspectCommandExitCode);

    final inspect = Inspect(fakeSsh);

    FakeAsync().run((fakeAsync) {
      final result = inspect.inspectComponentRoot(componentName);
      fakeAsync.flushTimers();

      if (shouldFailDueToFind || shouldFailDueToInspect) {
        expect(result, completion(isNull));
      } else {
        expect(result, completion(equals(contentsRoot)));
      }

      // Needed so that 'completion' above evaluates.
      fakeAsync.flushMicrotasks();
    });

    expect(fakeSsh.findCommandCount, equals(findCommandExitCode.length));
    expect(
        fakeSsh.inspectCommandCount, anyOf(0, inspectCommandExitCode.length));
  });
}

class FakeSsh implements Ssh {
  int findCommandCount = 0;
  int inspectCommandCount = 0;

  String findCommandStdOut;
  String expectedInspectSuffix;
  dynamic inspectCommandContentsRoot;
  List<int> findCommandExitCode;
  List<int> inspectCommandExitCode;

  FakeSsh({
    this.findCommandStdOut,
    this.expectedInspectSuffix,
    this.inspectCommandContentsRoot,
    this.findCommandExitCode = const <int>[0],
    this.inspectCommandExitCode = const <int>[0],
  });

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnsupportedError(invocation.toString());

  @override
  Future<ProcessResult> run(String command, {String stdin}) async {
    int exitCode;
    String stdout;

    if (command.trim() == _iqueryFindCommand) {
      exitCode = findCommandExitCode[findCommandCount++];
      stdout = findCommandStdOut;
    } else if (command.trim() ==
        '$_iqueryRecursiveInspectCommandPrefix $expectedInspectSuffix') {
      exitCode = inspectCommandExitCode[inspectCommandCount++];
      stdout = json.encode(inspectCommandContentsRoot);
    } else {
      print('got unknown command $command');
      exitCode = -1;
    }
    return ProcessResult(0, exitCode, stdout, '');
  }
}
