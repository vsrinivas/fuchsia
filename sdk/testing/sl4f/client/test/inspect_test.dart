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

  test('inspectComponentRoot returns after timeout is reached', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(FakeSsh(
        findCommandStdOut: 'one\n$componentName\nthree\n',
        inspectCommandContentsRoot: [
          {
            'contents': {'root': contentsRoot}
          },
        ],
        expectedInspectSuffix: componentName,
        fakeWait: Duration(seconds: 3)));

    final result = await inspect.retrieveHubEntries(
        filter: 'foo', cmdTimeout: Duration(seconds: 1));

    expect(result, isEmpty);
  });
  test('inspectComponentRoot is resiliant to old root layouts', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(FakeSsh(
        findCommandStdOut: 'one\n$componentName\nthree\n',
        inspectCommandContentsRoot: [
          {
            'contents': {
              'root': {'root': contentsRoot}
            }
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

  group(FreezeDetector, () {
    test('no freeze', () async {
      FakeAsync().run((fakeAsync) {
        final inspect = FakeInspect()..delay = const Duration(milliseconds: 10);
        final freezeDetector = FreezeDetector(inspect)
          ..threshold = const Duration(seconds: 1)
          ..start();

        fakeAsync.elapse(Duration(seconds: 1));
        freezeDetector.stop();
        fakeAsync.flushMicrotasks();

        expect(freezeDetector.freezeHappened(), isFalse);
      });
    });

    test('freeze', () async {
      FakeAsync().run((fakeAsync) {
        final inspect = FakeInspect()..delay = const Duration(seconds: 1);
        final freezeDetector = FreezeDetector(inspect)
          ..threshold = const Duration(milliseconds: 10)
          ..start();

        fakeAsync.elapse(Duration(seconds: 1));
        freezeDetector.stop();
        fakeAsync.flushMicrotasks();

        expect(freezeDetector.freezeHappened(), isTrue);
      });
    });

    test('waitUntilUnfrozen', () async {
      FakeAsync().run((fakeAsync) async {
        final inspect = FakeInspect()..delay = const Duration(seconds: 1);
        final freezeDetector = FreezeDetector(inspect)
          ..threshold = const Duration(milliseconds: 10);

        expect(freezeDetector.freezeHappened(), isFalse);
        freezeDetector.start();

        fakeAsync.elapse(Duration(milliseconds: 300));
        expect(freezeDetector.freezeHappened(), isTrue);
        expect(freezeDetector.isFrozen(), isTrue);
        freezeDetector.threshold = const Duration(seconds: 10);

        final unblocked = freezeDetector.waitUntilUnfrozen();
        fakeAsync.elapse(Duration(seconds: 5));
        await unblocked;

        freezeDetector.stop();
        fakeAsync.flushMicrotasks();

        expect(freezeDetector.isFrozen(), isFalse);
      });
    });
  });
}

class FakeInspect implements Inspect {
  @override
  final Ssh ssh = null;

  Duration delay;

  @override
  Future<dynamic> inspectComponentRoot(Pattern componentName) async {
    await Future.delayed(delay);
  }

  @override
  Future<dynamic> inspectRecursively(List<String> entries) async {
    await Future.delayed(delay);
  }

  @override
  Future<List<String>> retrieveHubEntries(
      {Pattern filter,
      Duration cmdTimeout = const Duration(seconds: 10)}) async {
    await Future.delayed(delay);
    return null;
  }
}

class FakeSsh implements Ssh {
  int findCommandCount = 0;
  int inspectCommandCount = 0;

  String findCommandStdOut;
  String expectedInspectSuffix;
  dynamic inspectCommandContentsRoot;
  List<int> findCommandExitCode;
  List<int> inspectCommandExitCode;
  Duration fakeWait;

  FakeSsh({
    this.findCommandStdOut,
    this.expectedInspectSuffix,
    this.inspectCommandContentsRoot,
    this.findCommandExitCode = const <int>[0],
    this.inspectCommandExitCode = const <int>[0],
    this.fakeWait,
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
    if (fakeWait != null) {
      await Future.delayed(fakeWait);
    }
    return ProcessResult(0, exitCode, stdout, '');
  }
}
