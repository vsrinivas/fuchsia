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
  final bool isVerbose;
  final bool simpleOutput;
  final bool hasRealTimeOutput;
  final Duration slowTestThreshold;
  final List<TestInfo> _infoEvents;
  final List<TestStarted> _testStartedEvents;
  final List<TestResult> _testResultEvents;
  final OutputBuffer buffer;
  final Stylizer wrapWith;
  DateTime _testSuiteStartTime;
  DateTime _lastTestStartTime;
  bool _hasStartedTests;
  bool _currentTestIsSlow;
  bool _lastTestHadOutput;
  int _numPassed;
  int _numFailed;

  OutputFormatter({
    @required this.isVerbose,
    @required this.hasRealTimeOutput,
    @required this.slowTestThreshold,
    @required this.wrapWith,
    this.simpleOutput = false,
    OutputBuffer buffer,
  })  : buffer = buffer ?? OutputBuffer.realIO(),
        _testResultEvents = [],
        _infoEvents = [],
        _testStartedEvents = [],
        _numPassed = 0,
        _numFailed = 0,
        _currentTestIsSlow = false,
        _hasStartedTests = false,
        _lastTestHadOutput = false;

  factory OutputFormatter.fromConfig(
    TestsConfig testsConfig, {
    OutputBuffer buffer,
  }) {
    if (testsConfig.flags.infoOnly) {
      return InfoFormatter();
    }
    var slowTestThreshold = testsConfig.flags.slowThreshold > 0
        ? Duration(seconds: testsConfig.flags.slowThreshold)
        : null;
    return StandardOutputFormatter(
      buffer: buffer,
      hasRealTimeOutput: testsConfig.flags.allOutput,
      isVerbose: testsConfig.flags.isVerbose || testsConfig.flags.allOutput,
      simpleOutput: testsConfig.flags.simpleOutput,
      slowTestThreshold: slowTestThreshold,
      wrapWith: testsConfig.wrapWith,
    );
  }

  bool get hasStartedTests => _hasStartedTests;
  int get numFailures => _numFailed;

  String get testExecutionTime => _getExecutionTime(_lastTestStartTime);
  String get suiteExecutionTime => _getExecutionTime(_testSuiteStartTime);

  String _getExecutionTime(DateTime _startTime) {
    Duration elapsedTime = DateTime.now().difference(_startTime);
    String minutes = elapsedTime.inMinutes.toString().padLeft(2, '0');
    String seconds = (elapsedTime.inSeconds % 60).toString().padLeft(2, '0');
    return '$minutes:$seconds';
  }

  String get ratioDisplay {
    var passed =
        wrapWith('PASS: $_numPassed', _numPassed > 0 ? [green] : [darkGray]);
    var failed =
        wrapWith('FAIL: $_numFailed', _numFailed > 0 ? [red] : [darkGray]);
    return '$passed $failed';
  }

  /// Future that resolves when the stdout closes for any reason
  Future get stdOutClosedFuture => buffer.stdOutClosedFuture();

  /// Pass-thru to the actual buffer's close method
  void close() => buffer.close();

  void forcefullyClose() => buffer.forcefullyClose();

  String addEmoji(Emoji emoji) {
    return simpleOutput ? emoji.fallback : emoji.emoji;
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
    var _now = DateTime.now();
    if (event is BeginningTests) {
      _hasStartedTests = true;
      _testSuiteStartTime = _now;
    } else if (event is GeneratingHintsEvent) {
      _hasStartedTests = true;
    } else if (event is TestStarted) {
      _currentTestIsSlow = false;
      _lastTestStartTime = _now;
      _handleTestStarted(event);
    } else if (event is TestResult) {
      (event.isSuccess) ? _numPassed += 1 : _numFailed += 1;
      _handleTestResult(event);
    } else if (event is TestOutputEvent) {
      // This must always come before `TestInfo`, because it is a subclass
      _handleTestOutputEvent(event);
    } else if (event is TimeElapsedEvent) {
      // We do nothing with time events during realtime output -- the user is
      // already getting constant updates
      if (hasRealTimeOutput) return;
      if (slowTestThreshold != null && event.timeElapsed >= slowTestThreshold) {
        if (!_currentTestIsSlow) {
          _currentTestIsSlow = true;
          _handleSlowTest(event);
        }
      }
      _handleTimeElapsedEvent(event);
    } else if (event is TestInfo) {
      _handleTestInfo(event);
    } else if (event is FatalError) {
      _handleFatalError(event);
    } else if (event is AllTestsCompleted) {
      _finalizeOutput();
    }
  }

  /// Handles every [TimeElapsedEvent] instance, regardless of whether the test
  /// has yet been deemed "slow".
  void _handleTimeElapsedEvent(TimeElapsedEvent event);

  /// Handles only the first [TimeElapsedEvent] that passes the slow threshold.
  void _handleSlowTest(TimeElapsedEvent event) {
    var hint =
        wrapWith('(adjust this value with the -s|--slow flag)', [darkGray]);
    buffer
      ..addLine(
        wrapWith(
            ' >> Runtime has exceeded ${slowTestThreshold.inSeconds} '
            'seconds $hint',
            [magenta]),
      )
      ..addLines(event.output);
  }

  /// Generic info events handler.
  void _handleTestInfo(TestInfo event);

  /// Handler for fatal errors.
  void _handleFatalError(FatalError event);

  /// Handler for the stream of stdout and stderr content produced by running
  /// tests.
  void _handleTestOutputEvent(TestOutputEvent event) {
    if (_currentTestIsSlow || hasRealTimeOutput) {
      _lastTestHadOutput = true;
      return _handleTestInfo(event);
    }
  }

  /// Handles the event that fires as each test beings execution.
  void _handleTestStarted(TestStarted event);

  /// Handles the event that fires as each test completes execution.
  void _handleTestResult(TestResult event);

  /// Produces summarizing content for the user. Called once.
  void _finalizeOutput();

  void _reportSummary() {
    buffer.reduceEmptyRowsTo(0);
    if (_testResultEvents.isEmpty) {
      buffer.addLines(['Ran zero tests']);
      return;
    }
    String failures = numFailures != 1 ? 'failures' : 'failure';
    String verb = _testResultEvents.last.isDryRun ? 'Faked' : 'Ran';
    var verboseHint = !isVerbose && !cleanEndOfOutput
        ? wrapWith(' (use the -v flag to see each test)', [darkGray])
        : '';
    String summary =
        '$verb ${_testResultEvents.length} tests with $numFailures $failures$verboseHint';
    if (numFailures == 0) {
      summary = '${addEmoji(Emoji.party)}  ${wrapWith(summary, [
        green
      ])} ${addEmoji(Emoji.party)}';
    }
    buffer.addLines([summary]);
  }

  bool get cleanEndOfOutput =>
      // Did the last test end with verbose output?
      !isVerbose &&
      !_currentTestIsSlow &&
      !hasRealTimeOutput &&
      // Did the last test fail?
      (_testResultEvents.isNotEmpty && _testResultEvents.last.isSuccess);
}

/// Default output wrapper inspired by both Dart's test runner and fx build's
/// standard output.
///
/// Aims to print something like this (when no tests fail):
/// ```txt
///  $ fx test --limit 2 -v
///    Found N total tests in //out/default/tests.json
///    Will run 2 tests
///
///    [2/2] 00:01 √ All tests completed
///
///    🎉  Ran 2 tests with 0 failures 🎉
/// ```
class StandardOutputFormatter extends OutputFormatter {
  StandardOutputFormatter({
    @required bool hasRealTimeOutput,
    @required Stylizer wrapWith,
    bool simpleOutput = true,
    Duration slowTestThreshold,
    OutputBuffer buffer,
    bool isVerbose,
  }) : super(
          isVerbose: isVerbose,
          hasRealTimeOutput: hasRealTimeOutput,
          simpleOutput: simpleOutput,
          slowTestThreshold: slowTestThreshold,
          buffer: buffer,
          wrapWith: wrapWith,
        );

  @override
  void _handleTestInfo(TestInfo event) {
    buffer.addLines([
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests && event.requiresPadding) ' ',
      // The message itself
      event.message,
      // Padding for info events that appear in the middle of the result stream
      if (hasStartedTests && event.requiresPadding) ' ',
    ]);
  }

  @override
  void _handleTimeElapsedEvent(TimeElapsedEvent event) {}

  @override
  void _handleTestStarted(TestStarted event) {
    String testName = wrapWith(event.testName, [cyan]);
    // Add some padding for our first test event
    if (_testStartedEvents.length == 1) {
      buffer.addLine('');
    }
    _lastTestHadOutput = false;

    var output =
        '$ratioDisplay $testExecutionTime ${addEmoji(Emoji.thinking)}  $testName';
    if (isVerbose) {
      buffer.addLine(output);
    } else {
      // Add some more padding for our first test event
      if (_testStartedEvents.length == 1) {
        buffer.addLine('');
      }
      buffer.updateLines([output]);
    }
  }

  /// Prints something like "[numPassed/numRun] TT:TT <emoji> <test-name>"
  @override
  void _handleTestResult(TestResult event) {
    if (_lastTestHadOutput) buffer.addLines(['']);

    String testName = wrapWith(event.testName, [cyan]);
    String emoji = event.isDryRun
        ? wrapWith('(dry run)', [darkGray])
        : event.isSuccess
            ? '${addEmoji(Emoji.check)} '
            : '${addEmoji(Emoji.x)} ';

    var output = '$ratioDisplay $testExecutionTime $emoji $testName';

    // When piping realtime output to the user, we don't want to remove the
    // last line. Usually, the last line is what indicates a new test is underway,
    // which we replace in favor of the results line, but in this scenario,
    // it's likely some output from the test.
    // We do the same for slow tests because we print realtime output for them.
    if (hasRealTimeOutput || _currentTestIsSlow) {
      buffer.addLines([output]);
    } else {
      buffer.updateLines([output]);
    }

    // Report for failed messages
    if (!event.isSuccess && event.message != null) {
      // But only if not already doing realtime
      if (!hasRealTimeOutput && !_currentTestIsSlow) {
        buffer.addLines([event.message, '']);
      }
    }
  }

  @override
  void _handleFatalError(FatalError event) {
    buffer.addLine(wrapWith(event.message, [red]));
  }

  void _finalizeLastTestLine() {
    if (!cleanEndOfOutput) return;
    var verboseHint = wrapWith(
      ' (use the -v flag to see each test)',
      [darkGray],
    );
    buffer.updateLines([
      '$ratioDisplay $suiteExecutionTime All tests completed$verboseHint',
    ]);
  }

  @override
  void _finalizeOutput() {
    _finalizeLastTestLine();
    _reportSummary();
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
          buffer: OutputBuffer.realIO(cursorStartsOnNewLine: true),
          hasRealTimeOutput: false,
          slowTestThreshold: Duration(seconds: 0),
          isVerbose: false,
          wrapWith: (value, _, {forScript}) => value,
        );

  @override
  void _handleTestInfo(TestInfo event) {}
  @override
  void _handleFatalError(FatalError event) {}
  @override
  void _handleTestStarted(TestStarted event) {
    buffer.addLines([
      ...infoPrint(event.testDefinition),
      '',
    ]);
  }

  @override
  void _handleTestResult(TestResult event) {}
  @override
  void _finalizeOutput() {}
  @override
  void _handleTimeElapsedEvent(TimeElapsedEvent event) {}
}

List<String> infoPrint(TestDefinition testDefinition) {
  var command = testDefinition
      .createExecutionHandle()
      .getInvocationTokens(const []).toString();
  return <String>[
    _isTruthy(command) ? 'command: $command' : null,
    _isTruthy(testDefinition.cpu) ? 'cpu: ${testDefinition.cpu}' : null,
    _isTruthy(testDefinition.runtimeDeps)
        ? 'runtime_deps: ${testDefinition.runtimeDeps}'
        : null,
    _isTruthy(testDefinition.name) ? 'name: ${testDefinition.name}' : null,
    _isTruthy(testDefinition.os) ? 'os: ${testDefinition.os}' : null,
    testDefinition.packageUrl != null
        ? 'package_url: ${testDefinition.packageUrl}'
        : null,
    _isTruthy(testDefinition.label) ? 'label: ${testDefinition.label}' : null,
    _isTruthy(testDefinition.path) ? 'path: ${testDefinition.path}' : null,
  ].where((_val) => _val != null).toList()
    ..sort();
}

/// Variant [OutputFormatter] which writes all content to a file instead of
/// to the usual file descriptor [stdout].
///
/// [FileFormatter] accomplishes its task by accepting a file path as its lone
/// constructor argument and using that to hydrate a special, file-based version
/// of its [OutputBuffer].
class FileFormatter extends StandardOutputFormatter {
  /// Internal constructor which sets all variables appropriately for writing
  /// output to a file.
  FileFormatter._({@required String path})
      : assert(path != null),
        super(
          isVerbose: true,
          hasRealTimeOutput: true,
          slowTestThreshold: null,
          simpleOutput: true,
          wrapWith: (v, _, {forScript}) => v,
          buffer: OutputBuffer.fileIO(path: path),
        );

  /// Main (and currently only) constructor which accepts a [TestsConfig] object
  /// and either instantiates and returns an object if appropriate, or [null] if
  /// no file logging is desired.
  factory FileFormatter.fromConfig(TestsConfig testsConfig) {
    if (testsConfig.flags.shouldLog) {
      return FileFormatter._(
        path: testsConfig.flags.logPath ?? testsConfig.fxEnv.outputDir,
      );
    }
    return null;
  }
}

bool _isTruthy(String val) => val != null && val != '';
