// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_feedback/fidl_async.dart';
import 'package:fidl_fuchsia_mem/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:zircon/zircon.dart';

/// Defines a class to run programs under an error [Zone].
///
/// Unhandled errors are reported to |fuchsia.feedback.CrashReporter| service.
class CrashReportingRunner {
  CrashReporter? reporter;
  SizedVmo Function(String)? vmoBuilder;

  CrashReportingRunner({this.reporter, this.vmoBuilder});

  /// Run the function [body] in a error zone to catch unhandled errors.
  Future<void> run(Future<void> Function() body) async {
    runZonedGuarded(
      () {
        FlutterError.onError = _report;
        // Use try-catch for non-flutter framework errors or synchronous errors.
        try {
          body();
          // ignore: avoid_catches_without_on_clauses
        } catch (e, stackTrace) {
          // Never ever use error-handling-zones in user code:
          // go/no-error-handling-zones.
          throw _SynchronousException(e, stackTrace);
        }
      },
      _onError,
    );
  }

  // Creates and files a [CrashReport] upon receiving [FlutterErrorDetails].
  void _report(FlutterErrorDetails details) {
    final uptime = System.clockGetMonotonic();

    final error = details.exception.toString();
    var errorType = 'UnknownError';
    var errorMessage = error;
    final index = error.toString().indexOf(':');
    if (index >= 0) {
      errorType = error.substring(0, index);
      errorMessage =
          error.substring(index + 2 /*to get rid of the leading ': '*/);
    }

    // Optionally you can print the error to console. This is the default
    // Flutter behavior for uncaught errors.
    // This won't dump anything to console if flutter_build_mode=release
    FlutterError.dumpErrorToConsole(details, forceReport: true);

    // Convert stacktrace to VMO.
    vmoBuilder ??= _vmoFromStackTrace;
    final vmo = vmoBuilder!(details.stack.toString());

    // Generate [CrashReport] from errorType, errorMessage and stackTrace.
    final report = CrashReport(
      programName: 'fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cm',
      programUptime: uptime,
      specificReport: SpecificCrashReport.withDart(
        RuntimeCrashReport(
          exceptionType: errorType,
          exceptionMessage: errorMessage,
          exceptionStackTrace: Buffer(vmo: vmo, size: vmo.size!),
        ),
      ),
    );

    // File the report.
    _fileReport(report);
  }

  Future<void> _fileReport(CrashReport report) async {
    log.severe('Caught unhandled error in ermine. Generating crash report');
    if (reporter != null) {
      await reporter!.file(report);
    } else {
      final reporterProxy = CrashReporterProxy();
      final incoming = Incoming.fromSvcPath()..connectToService(reporterProxy);
      await reporterProxy.file(report);
      reporterProxy.ctrl.close();
      await incoming.close();
    }
  }

  // Handles error thrown by [run].
  void _onError(Object error, StackTrace stackTrace) {
    var realError = error;
    var realStackTrace = stackTrace;
    if (error is _SynchronousException) {
      realError = error.cause;
      realStackTrace = error.stack;
    }

    // Pipes the error over to [FlutterError], which will run the custom
    // onError handler.
    FlutterError.reportError(
        FlutterErrorDetails(exception: realError, stack: realStackTrace));

    // Just logging and ignoring an uncaught synchronous exception is not safe.
    // After unwinding the stack, Agents and features might be in an
    // arbitrarily inconsistent state.
    if (error is _SynchronousException) {
      throw error;
    }
  }
}

/// Returns a [SizedVmo] from String.
SizedVmo _vmoFromStackTrace(String stackTrace) {
  final list = Uint8List.fromList(stackTrace.codeUnits);
  return SizedVmo.fromUint8List(list);
}

/// An exception wrapper to indicate synchronous exceptions thrown from main().
class _SynchronousException implements Exception {
  dynamic cause;
  StackTrace stack;
  _SynchronousException(this.cause, this.stack);
}
