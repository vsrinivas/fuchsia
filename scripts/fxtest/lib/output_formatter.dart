// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';

abstract class OutputFormatter {
  final bool verbose;
  final bool shouldColorizeOutput;
  final bool shouldShowPassedTestsOutput;
  final Duration slowTestThreshold;
  final List<TestInfo> _infoEvents;
  final List<TestStarted> _testStartedEvents;
  final List<TestResult> _testResultEvents;
  final OutputBuffer _buffer;
  bool _hasStartedTests;

  OutputFormatter({
    this.verbose,
    this.shouldColorizeOutput = true,
    this.shouldShowPassedTestsOutput = false,
    this.slowTestThreshold,
    OutputBuffer buffer,
  })  : _testResultEvents = [],
        _infoEvents = [],
        _testStartedEvents = [],
        _buffer = buffer ?? OutputBuffer(),
        _hasStartedTests = false;

  bool get hasStartedTests => _hasStartedTests;
  int get numFailures => _testResultEvents.where((ev) => !ev.isSuccess).length;

  String colorize(String content, Iterable<AnsiCode> args) {
    return shouldColorizeOutput ? wrapWith(content, args) : content;
  }

  String addEmoji(String emoji, [String fallback]) {
    return shouldColorizeOutput ? emoji : fallback ?? '';
  }

  /// Accepts a [TestEvent] and updates the output buffer.
  void update(TestEvent event) {
    if (event is TestInfo) {
      _infoEvents.add(event);
    } else if (event is TestStarted) {
      _testStartedEvents.add(event);
    } else if (event is TestResult) {
      _testResultEvents.add(event);
    }
    _updateAfterEvent(event);
  }

  void _updateAfterEvent(TestEvent event) {
    if (event is BeginningTests) {
      _hasStartedTests = true;
    } else if (event is TestStarted) {
      _handleTestStarted(event);
    } else if (event is TestResult) {
      _handleTestResult(event);
    } else if (event is TestInfo) {
      _handleTestInfo(event);
    } else if (event is AllTestsCompleted) {
      _finalizeOutput();
    }
  }

  void _handleTestInfo(TestInfo event);
  void _handleTestStarted(TestStarted event);
  void _handleTestResult(TestResult event);
  void _finalizeOutput();

  void _reportSummary() {
    if (_testResultEvents.isEmpty) {
      _buffer.addLines(['', 'Ran zero tests']);
      return;
    }
    String failures = numFailures != 1 ? 'failures' : 'failure';
    String verb = _testResultEvents.last.isDryRun ? 'Faked' : 'Ran';
    String summary =
        '$verb ${_testResultEvents.length} tests with $numFailures $failures';
    if (numFailures == 0) {
      summary =
          '${addEmoji('🎉')}  ${colorize(summary, [green])} ${addEmoji('🎉')}';
    }
    _buffer.addLines(['', summary]);
  }

  void _reportFailedTests() {
    if (numFailures == 0) {
      return;
    }
    var failed = [
      for (var event in _testResultEvents)
        if (!event.isSuccess) //
          event.message
    ];
    _buffer.addLines([
      ' ',
      ...failed,
    ]);
  }

  static String humanizeRuntime(Duration runtime) {
    List<String> tokens = [];
    Duration remainingTime = runtime;
    int numHours = remainingTime.inHours;
    if (numHours > 0) {
      remainingTime = remainingTime ~/ (Duration.secondsPerHour * numHours);
      tokens.add('$numHours hour${numHours > 1 ? "s" : ""}');
    }
    int numMinutes = remainingTime.inMinutes;
    if (numMinutes > 0) {
      remainingTime = remainingTime ~/ (Duration.secondsPerMinute * numMinutes);
      tokens.add('$numMinutes minute${numMinutes > 1 ? "s" : ""}');
    }
    int numSeconds = remainingTime.inSeconds;
    if (numSeconds > 0) {
      tokens.add('$numSeconds second${numSeconds > 1 ? "s" : ""}');
    }
    return tokens.join(', ');
  }
}

/// Output wrapper that values sharing as much info as possible.
///
/// Aims to print something like this:
/// ```txt
///  $ fx test --limit 2 -v
///    Found N total tests in //out/default/tests.json
///    Will run 2 tests
///
///     > √ fx run-test test_1_name
///     > √ fx run-test test_2_name
///
///    🎉  Ran 2 tests with 0 failures 🎉
/// ```
class VerboseOutputFormatter extends OutputFormatter {
  VerboseOutputFormatter({
    shouldColorizeOutput = true,
    shouldShowPassedTestsOutput,
    slowTestThreshold,
    OutputBuffer buffer,
  }) : super(
          verbose: true,
          shouldColorizeOutput: shouldColorizeOutput,
          shouldShowPassedTestsOutput: shouldShowPassedTestsOutput,
          slowTestThreshold: slowTestThreshold,
          buffer: buffer,
        );

  @override
  void _handleTestInfo(TestInfo event) {
    _buffer.addLines([
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests)
        ' ',
      // The message itself
      event.message,
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests)
        ' ',
    ]);
  }

  @override
  void _handleTestStarted(TestStarted event) {
    String testName = colorize(event.testName, [cyan]);
    // Add sine padding for our first test event
    if (_testStartedEvents.length == 1) {
      _buffer.addLine('');
    }
    _buffer.addSubstring('${addEmoji('🤔', '?')} $testName');
  }

  @override
  void _handleTestResult(TestResult event) {
    String testName = colorize(event.testName, [cyan]);
    String emoji = event.isDryRun
        ? colorize('(dry run)', [darkGray])
        : event.isSuccess ? colorize('√', [green]) : colorize('F', [red]);

    String runtime =
        slowTestThreshold != null && event.runtime > slowTestThreshold
            ? colorize(
                ' (${OutputFormatter.humanizeRuntime(event.runtime)})',
                [lightRed, styleBold],
              )
            : '';

    _buffer.updateLines(['$emoji $testName$runtime']);

    // `stdout` content is included in the message for a failed test, so we
    // would be duplicating that for failed tests
    if (shouldShowPassedTestsOutput && event.isSuccess) {
      _buffer.addLines(event.message.split('\n'));
    }
  }

  @override
  void _finalizeOutput() {
    _reportFailedTests();
    _reportSummary();
  }
}

/// Output wrapper that favors reduced output size.
///
/// Aims to print something like this:
/// ```txt
///  $ fx test --limit 2
///    Found N total tests in //out/default/tests.json
///    Will run 2 tests
///
///     ..
///
///    🎉  Ran 2 tests with 0 failures 🎉
/// ```
class CondensedOutputFormatter extends OutputFormatter {
  final List<TestInfo> _hiddenInfo;

  CondensedOutputFormatter({
    shouldColorizeOutput = true,
    slowTestThreshold,
    OutputBuffer buffer,
  })  : _hiddenInfo = [],
        super(
          verbose: false,
          shouldColorizeOutput: shouldColorizeOutput,
          slowTestThreshold: slowTestThreshold,
          buffer: buffer,
        );

  @override
  void _handleTestStarted(TestStarted event) {
    if (_testStartedEvents.length == 1) {
      _buffer.addLine('');
    }
  }

  @override
  void _handleTestResult(TestResult event) {
    _buffer.addSubstring(event.isSuccess
        ? colorize('.', event.isDryRun ? [darkGray] : [])
        : colorize('F', [red]));
  }

  @override
  void _handleTestInfo(TestInfo event) {
    // Don't print any further info messages once we start running tests,
    // since we're going to spit them all out at the end
    if (!hasStartedTests) {
      _buffer.addLine(event.message);
    } else {
      _hiddenInfo.add(event);
    }
  }

  @override
  void _finalizeOutput() {
    _reportSquashedInfo();
    _reportFailedTests();
    _reportSummary();
  }

  void _reportSquashedInfo() {
    if (_hiddenInfo.isNotEmpty) {
      for (TestInfo event in _hiddenInfo) {
        _buffer.addLine(event.message);
      }
      _buffer.addLine('');
    }

    if (slowTestThreshold != null) {
      List<String> slowTests = [];
      for (TestResult event in _testResultEvents) {
        if (event.runtime > slowTestThreshold) {
          String runtime = colorize(
              '(${OutputFormatter.humanizeRuntime(event.runtime)})',
              [darkGray, styleBold]);
          slowTests.add(
            '${event.testName} $runtime',
          );
        }
      }
      if (slowTests.isNotEmpty) {
        _buffer.addLines([
          '',
          ...slowTests,
        ]);
      }
    }
  }
}
