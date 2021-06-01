// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/utils/crash.dart';
import 'package:fidl_fuchsia_feedback/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  setupLogger(name: 'ermine_unittests');

  test('crash and report synchronous errors', () async {
    final mockReporter = MockReporter();
    final runner = CrashReportingRunner(
      reporter: mockReporter,
      vmoBuilder: (stackTrace) => SizedVmo(null, 10),
    );

    // Simulate a synchronous exception thrown by main. The runner should catch
    // and report the exception. And then rethrow the synchronous exception.
    expect(() async {
      await runner.run(() {
        throw UnimplementedError('boom');
      });
    }, throwsException);

    CrashReport report = verify(mockReporter.file(captureAny)).captured.single;
    expect(report.programName, 'ermine.cmx');
    expect(report.specificReport.dart.exceptionType, 'UnimplementedError');
    expect(report.specificReport.dart.exceptionMessage, 'boom');
    expect(report.specificReport.dart.exceptionStackTrace.size, 10);
  });

  test('crash and report async errors', () async {
    final mockReporter = MockReporter();
    final runner = CrashReportingRunner(
      reporter: mockReporter,
      vmoBuilder: (stackTrace) => SizedVmo(null, 10),
    );

    // Simulate an async exception thrown by main. The runner should catch and
    // report the exception.
    await runner.run(() async {
      throw UnimplementedError('boom');
    });

    CrashReport report = verify(mockReporter.file(captureAny)).captured.single;
    expect(report.programName, 'ermine.cmx');
    expect(report.specificReport.dart.exceptionType, 'UnimplementedError');
    expect(report.specificReport.dart.exceptionMessage, 'boom');
    expect(report.specificReport.dart.exceptionStackTrace.size, 10);
  });

  test('crash and report delayed async errors', () async {
    final mockReporter = MockReporter();
    final runner = CrashReportingRunner(
      reporter: mockReporter,
      vmoBuilder: (stackTrace) => SizedVmo(null, 10),
    );

    // Simulate a delayed async exception thrown by main. The runner should
    // catch and report the exception.
    await runner.run(() async {
      await Future.delayed(Duration(milliseconds: 0), () async {
        throw UnimplementedError('boom');
      });
    });

    await Future.delayed(Duration(milliseconds: 5));

    CrashReport report = verify(mockReporter.file(captureAny)).captured.single;
    expect(report.programName, 'ermine.cmx');
    expect(report.specificReport.dart.exceptionType, 'UnimplementedError');
    expect(report.specificReport.dart.exceptionMessage, 'boom');
    expect(report.specificReport.dart.exceptionStackTrace.size, 10);
  });
}

class MockReporter extends Mock implements CrashReporter {}
