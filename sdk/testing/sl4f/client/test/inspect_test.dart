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
  HttpServer fakeServer;
  Sl4f sl4f;

  setUp(() async {
    fakeServer = await HttpServer.bind('127.0.0.1', 18080);
    sl4f = Sl4f('127.0.0.1', null, 18080);
  });

  tearDown(() async {
    await fakeServer.close();
  });

  test('snapshot inspect', () async {
    final selectors = ['test.cmx:root', 'other.cmx:root/node:prop'];
    final expectedHierarchies = [
      {
        'contents': {
          'root': {'a': 1}
        },
        'path': 'test.cmx'
      },
      {
        'contents': {
          'root': {
            'node': {'prop': 2}
          }
        },
        'path': 'other.cmx'
      }
    ];
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], selectors);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': expectedHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshot(selectors);
    expect(result, equals(expectedHierarchies));
  });

  test('snapshot inspect root', () async {
    final resultHierarchies = [
      {
        'contents': {
          'root': {'a': 1}
        },
        'path': 'test.cmx'
      },
      {
        'contents': {
          'root': {
            'node': {'prop': 2}
          }
        },
        'path': 'other.cmx'
      }
    ];
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['test.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': resultHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshotRoot('test.cmx');
    expect(result, equals({'a': 1}));
  });

  test('inspectComponentRoot returns json decoded stdout', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(
      FakeSsh(
          findCommandStdOut: 'one\n$componentName\nthree\n',
          inspectCommandContentsRoot: [
            {
              'contents': {'root': contentsRoot}
            },
          ],
          expectedInspectSuffix: componentName),
    );

    final result = await inspect.inspectComponentRoot(componentName);

    expect(result, equals(contentsRoot));
  });

  test('inspectComponentRoot is resiliant to old root layouts', () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(
      FakeSsh(
          findCommandStdOut: 'one\n$componentName\nthree\n',
          inspectCommandContentsRoot: [
            {
              'contents': {
                'root': {'root': contentsRoot}
              }
            },
          ],
          expectedInspectSuffix: componentName),
    );

    final result = await inspect.inspectComponentRoot(componentName);

    expect(result, equals(contentsRoot));
  });

  test('inspectComponentRoot fails if multiple components with same name',
      () async {
    const componentName = 'foo';
    const contentsRoot = {'faz': 'bear'};

    final inspect = Inspect(
      FakeSsh(
          findCommandStdOut: 'one\n$componentName\n$componentName\n',
          inspectCommandContentsRoot: [
            {
              'contents': {'root': contentsRoot}
            },
            {
              'contents': {'root': contentsRoot}
            }
          ],
          expectedInspectSuffix: '$componentName $componentName'),
    );

    try {
      await inspect.inspectComponentRoot(componentName);

      fail('inspectComponentRoot didn\'t throw exception as was expected');
      // ignore: avoid_catching_errors
    } on Error {
      // Pass through for expected exception.
    }
  });

  test('inspectComponentRoot fails if there is stderr data', () async {
    final inspect = Inspect(
      FakeSsh(
          findCommandStdOut: '',
          findCommandStdErr: 'Error: blah!',
          inspectCommandContentsRoot: null),
    );

    final result = await inspect.inspectComponentRoot('anything.cmx');
    expect(result, isNull);
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
  Ssh ssh;

  @override
  Sl4f sl4f;

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
  Future<List<String>> retrieveHubEntries({Pattern filter}) async {
    await Future.delayed(delay);
    return null;
  }

  @override
  Future<List<Map<String, dynamic>>> snapshot(List<String> selectors) async {
    await Future.delayed(delay);
    return null;
  }

  @override
  Future<Map<String, dynamic>> snapshotRoot(String componentName) async {
    await Future.delayed(delay);
    return null;
  }
}

class FakeSsh implements Ssh {
  int findCommandCount = 0;
  int inspectCommandCount = 0;

  String findCommandStdOut;
  String findCommandStdErr;
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
    this.findCommandStdErr = '',
  });

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnsupportedError(invocation.toString());

  @override
  Future<ProcessResult> run(String command, {String stdin}) async {
    int exitCode;
    String stdout;
    String stderr;

    if (command.trim() == _iqueryFindCommand) {
      exitCode = findCommandExitCode[findCommandCount++];
      stdout = findCommandStdOut;
      stderr = findCommandStdErr;
    } else if (command.trim() ==
        '$_iqueryRecursiveInspectCommandPrefix $expectedInspectSuffix') {
      exitCode = inspectCommandExitCode[inspectCommandCount++];
      stdout = json.encode(inspectCommandContentsRoot);
    } else {
      print('got unknown command $command');
      exitCode = -1;
    }
    return ProcessResult(0, exitCode, stdout, stderr);
  }
}
