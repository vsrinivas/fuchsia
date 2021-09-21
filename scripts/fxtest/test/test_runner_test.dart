// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:pedantic/pedantic.dart';
import 'package:test/test.dart';

void main() {
  test('waits for test process to terminate before closing StreamController',
      () async {
    var processExitCode = Completer<int>();
    var mockProcess =
        mockStartProcess(MockProcess(exitCode: processExitCode.future));
    var testRunner = TestRunner(startProcess: mockProcess);

    // Drain output as it's generated. This prevents the StreamController from
    // blocking on close.
    testRunner.output.listen((event) {});

    var result = testRunner.run('', [], workingDirectory: '');

    // [result] won't complete until the mock process ends, so we don't await it
    // here.
    var completed = false;
    unawaited(result.whenComplete(() => completed = true));
    expect(completed, false);

    // Terminate the mock process.
    processExitCode.complete(0);

    // Verify that the result has now completed.
    await result;
    expect(completed, true);
  });

  test('waits for stdout to close before closing StreamController', () async {
    var stdout = StreamController<List<int>>();
    var mockProcess = mockStartProcess(MockProcess(stdout: stdout.stream));
    var testRunner = TestRunner(startProcess: mockProcess);

    // Drain output as it's generated. This prevents the StreamController from
    // blocking on close.
    testRunner.output.listen((event) {});

    var result = testRunner.run('', [], workingDirectory: '');

    // [result] won't complete until the mock process ends, so we don't await it
    // here.
    var completed = false;
    unawaited(result.whenComplete(() => completed = true));
    expect(completed, false);

    // Complete the stdout stream. This should cause the result to complete.
    await stdout.close();

    // Verify that the result has now completed.
    await result;
    expect(completed, true);
  });

  test('waits for stderr to close before closing StreamController', () async {
    var stderr = StreamController<List<int>>();
    var mockProcess = mockStartProcess(MockProcess(stderr: stderr.stream));
    var testRunner = TestRunner(startProcess: mockProcess);

    // Drain output as it's generated. This prevents the StreamController from
    // blocking on close.
    testRunner.output.listen((event) {});

    var result = testRunner.run('', [], workingDirectory: '');

    // [result] won't complete until the mock process ends, so we don't await it
    // here.
    var completed = false;
    unawaited(result.whenComplete(() => completed = true));
    expect(completed, false);

    // Complete the stderr stream.
    await stderr.close();

    // Verify that the result has now completed.
    await result;
    expect(completed, true);
  });
}
