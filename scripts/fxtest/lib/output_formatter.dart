// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:meta/meta.dart';

class Emoji {
  final String emoji;
  final String fallback;
  Emoji(this.emoji, this.fallback);

  static final Emoji party = Emoji('🎉', '!');
  static final Emoji thinking = Emoji('🤔', '?');
  static final Emoji check = Emoji('✅', '√');
  static final Emoji x = Emoji('❌', 'F');
}

abstract class OutputFormatter {
  final bool verbose;
  final bool shouldColorizeOutput;
  final bool hasRealTimeOutput;
  final Duration slowTestThreshold;
  final List<TestInfo> _infoEvents;
  final List<TestStarted> _testStartedEvents;
  final List<TestResult> _testResultEvents;
  final OutputBuffer _buffer;
  bool _hasStartedTests;

  OutputFormatter({
    @required this.verbose,
    @required this.hasRealTimeOutput,
    @required this.slowTestThreshold,
    this.shouldColorizeOutput = true,
    OutputBuffer buffer,
  })  : _testResultEvents = [],
        _infoEvents = [],
        _testStartedEvents = [],
        _buffer = buffer ?? OutputBuffer(),
        _hasStartedTests = false;

  bool get hasStartedTests => _hasStartedTests;
  int get numFailures => _testResultEvents.where((ev) => !ev.isSuccess).length;

  /// Future that resolves when the stdout closes for any reason
  Future get stdOutClosedFuture => _buffer.stdOutClosedFuture();

  /// Pass-thru to the actual buffer's close method
  void close() => _buffer.close();

  void forcefullyClose() => _buffer.forcefullyClose();

  String colorize(String content, Iterable<AnsiCode> args) {
    return shouldColorizeOutput ? wrapWith(content, args) : content;
  }

  String addEmoji(Emoji emoji) {
    return shouldColorizeOutput ? emoji.emoji : emoji.fallback;
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
      summary = '${addEmoji(Emoji.party)}  ${colorize(summary, [
        green
      ])} ${addEmoji(Emoji.party)}';
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
    @required bool hasRealTimeOutput,
    bool shouldColorizeOutput = true,
    Duration slowTestThreshold,
    OutputBuffer buffer,
  }) : super(
          verbose: true,
          hasRealTimeOutput: hasRealTimeOutput,
          shouldColorizeOutput: shouldColorizeOutput,
          slowTestThreshold: slowTestThreshold,
          buffer: buffer,
        );

  @override
  void _handleTestInfo(TestInfo event) {
    _buffer.addLines([
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests && event.requiresPadding)
        ' ',
      // The message itself
      event.message,
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests && event.requiresPadding)
        ' ',
    ]);
  }

  @override
  void _handleTestStarted(TestStarted event) {
    String testName = colorize(event.testName, [cyan]);
    // Add some padding for our first test event
    if (_testStartedEvents.length == 1) {
      _buffer.addLine('');
    }
    _buffer.addSubstring('${addEmoji(Emoji.thinking)}  $testName');
  }

  @override
  void _handleTestResult(TestResult event) {
    String testName = colorize(event.testName, [cyan]);
    String emoji = event.isDryRun
        ? colorize('(dry run)', [darkGray])
        : event.isSuccess
            ? '${addEmoji(Emoji.check)} '
            : '${addEmoji(Emoji.x)} ';

    String runtime =
        slowTestThreshold != null && event.runtime > slowTestThreshold
            ? colorize(
                ' (${OutputFormatter.humanizeRuntime(event.runtime)})',
                [lightRed, styleBold],
              )
            : '';

    // When piping realtime output to the user, we don't want to remove the
    // last line. Usually, the last line is the ":thinking_emoji: <test info>"
    // line, which we replace in favor of the results line, but in this scenario,
    // it's likely some output from the test.
    // _buffer.updateLines(['$emoji $testName$runtime']);
    if (hasRealTimeOutput) {
      _buffer.addLines(['$emoji $testName$runtime']);
    } else {
      _buffer.updateLines(['$emoji $testName$runtime']);
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
          hasRealTimeOutput: false,
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

/// Special formatter for when the caller is only using this tool to translate
/// shorthand test invocation patterns into fully-formed patterns.
///
/// This formatter is not intended to be paired with an actual test suite
/// execution, and as such, ignores all events except `TestStarted`, which it
/// uses to print a fully hydrated test invocation pattern.
class InfoFormatter extends OutputFormatter {
  InfoFormatter()
      : super(
          buffer: OutputBuffer(cursorStartsOnNewLine: true),
          hasRealTimeOutput: false,
          slowTestThreshold: Duration(seconds: 0),
          verbose: false,
        );

  @override
  void _handleTestInfo(TestInfo event) {}
  @override
  void _handleTestStarted(TestStarted event) {
    _buffer.addLines([
      ...infoPrint(event.testDefinition),
      '',
    ]);
  }

  @override
  void _handleTestResult(TestResult event) {}
  @override
  void _finalizeOutput() {}
}

List<String> infoPrint(TestDefinition testDefinition) {
  var command =
      testDefinition.executionHandle.getInvocationTokens(const []).toString();
  return <String>[
    _isTruthy(command) ? 'command: $command' : null,
    _isTruthy(testDefinition.cpu) ? 'cpu: ${testDefinition.cpu}' : null,
    _isTruthy(testDefinition.depsFile)
        ? 'depsFile: ${testDefinition.depsFile}'
        : null,
    _isTruthy(testDefinition.name) ? 'name: ${testDefinition.name}' : null,
    _isTruthy(testDefinition.os) ? 'os: ${testDefinition.os}' : null,
    _isTruthy(testDefinition.packageUrl)
        ? 'package_url: ${testDefinition.packageUrl}'
        : null,
    _isTruthy(testDefinition.label) ? 'label: ${testDefinition.label}' : null,
    _isTruthy(testDefinition.path) ? 'path: ${testDefinition.path}' : null,
  ].where((_val) => _val != null).toList()
    ..sort();
}

bool _isTruthy(String val) => val != null && val != '';
