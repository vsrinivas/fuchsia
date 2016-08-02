// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:semaphore/semaphore.dart';

final int kProcessParallelizationLimit = Platform.numberOfProcessors;
final LocalSemaphore _nonBlockingProcessSemaphore =
    new LocalSemaphore(kProcessParallelizationLimit);

final LocalSemaphore _blockingProcessSemaphore = new LocalSemaphore(1);

/// Runs a process asynchronously, displaying its output as it is produced.
/// Completes with the exit value of the launched process.
Future<int> process(String command, List<String> arguments,
    {String workingDirectory: '.', Map<String, String> environment}) async {
  await _blockingProcessSemaphore.acquire();
  return Process
      .start(command, arguments,
          workingDirectory: workingDirectory, environment: environment)
      .then((Process process) {
    stdout.addStream(process.stdout);
    stderr.addStream(process.stderr);
    return process.exitCode.whenComplete(_blockingProcessSemaphore.release);
  });
}

/// Runs a process asynchronously, displaying its output after the process
/// completes. Completes with the exit value of the launched process.
Future<int> processNonBlocking(String command, List<String> arguments,
    {String workingDirectory: '.', Map<String, String> environment}) async {
  return processNonBlockingWithResult(command, arguments,
      workingDirectory: workingDirectory,
      environment: environment).then((result) {
    stdout.write(result.stdout);
    stderr.write(result.stderr);
    return result.exitCode;
  });
}

/// Same as above, except that it returns the |ProcessResult| from the completed
/// process instead of displaying the result.
Future<ProcessResult> processNonBlockingWithResult(
    String command, List<String> arguments,
    {String workingDirectory: '.', Map<String, String> environment}) async {
  await _nonBlockingProcessSemaphore.acquire();
  return Process
      .run(command, arguments,
          workingDirectory: workingDirectory, environment: environment)
      .whenComplete(_nonBlockingProcessSemaphore.release);
}

/// A |Step| is an asynchronous computation returning an |int|.
typedef Future<int> Step();

/// Runs a list of |Step|s, waiting for each to complete. This behaves rather
/// like |foo && bar && baz| in a shell script. Returns 0 if all commands do,
/// and otherwise returns the exit code of the failing command.
Future<int> pipeline(List<Step> steps) async {
  for (final Step step in steps) {
    final int exitCode = await step();
    if (exitCode != 0) {
      return exitCode;
    }
  }
  return 0;
}

/// Runs a list of |Step|s in parallel, waiting for all to complete.
/// Returns 0 if all commands do, otherwise returns 1.
Future<int> parallel(List<Step> steps) async {
  if ((await Future.wait(steps.map((final Step step) => step())))
      .any((ret) => ret != 0)) {
    return 1;
  }
  return 0;
}

/// Runs a stream of |Step|s in parallel, waiting for all to complete.
/// Returns 0 if all commands do, otherwise returns 1.
Future<int> parallelStream(final Stream args, final Function test) async {
  List<Future<int>> futures = [];
  await for (final arg in args) {
    futures.add(test(arg));
  }
  if ((await Future.wait(futures)).any((ret) => ret != 0)) {
    return 1;
  }
  return 0;
}
