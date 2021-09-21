// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
import 'dart:async';
import 'dart:io';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

abstract class BaseOutput {
  final String content;
  BaseOutput(this.content);
}

/// Content for the stdout.
class Output extends BaseOutput {
  Output(String content) : super(content);
  @override
  String toString() => '<Output "$content">';
}

/// Content for the stderr.
class ErrOutput extends BaseOutput {
  ErrOutput(String content) : super(content);
  @override
  String toString() => '<ErrOutput "$content">';
}

/// Fake "TestRunner" which emits scripted output at scripted intervals.
class ScriptedTestRunner extends TestRunner {
  /// Series of [BaseOutput]s and [Duration]s which this class will funnel into
  /// [realtimeOutputSink] and [realtimeErrorSink].
  final List scriptedOutput;
  final int exitCode;
  final int pid;

  ScriptedTestRunner({
    this.scriptedOutput,
    this.exitCode = 0,
    this.pid = 1,
  }) : super();

  Future<ProcessResult> easyRun() {
    return run(
      '/some-cmd',
      ['meaningless', 'args'],
      workingDirectory: '/whatever',
    );
  }

  @override
  Future<ProcessResult> run(
    String command,
    List<String> args, {
    @required String workingDirectory,
    Map<String, String> environment,
  }) async {
    var _out = StringBuffer();
    var _err = StringBuffer();
    for (var _output in scriptedOutput) {
      if (_output is Output) {
        _out.writeln(_output.content);
        addOutput(_output.content);
      } else if (_output is ErrOutput) {
        _err.writeln(_output.content);
        addOutput(_output.content);
        // addError(_output.content);
      } else if (_output is Duration) {
        await Future.delayed(_output);
      } else {
        throw Exception('`output` variable must only contain `BaseOutput` '
            'and `Duration` instances');
      }
    }
    return ProcessResult(pid, exitCode, _out.toString(), _err.toString());
  }
}

/// Fake version of [TestRunner] whose [run] method returns a contrived
/// [ProcessResult] instance without ever creating a process.
class FakeTestRunner extends TestRunner {
  final int exitCode;

  FakeTestRunner._(this.exitCode) : super();
  factory FakeTestRunner.passing() => FakeTestRunner._(0);
  factory FakeTestRunner.failing() => FakeTestRunner._(2);

  @override
  Future<ProcessResult> run(
    String command,
    List<String> args, {
    @required String workingDirectory,
    Map<String, String> environment,
  }) async {
    String _stdout = args.join(' ');
    addOutput(_stdout);

    String _stderr = workingDirectory?.toString();
    if (_stderr != null) addOutput(_stderr);

    await close();

    return Future.value(ProcessResult(1, exitCode, _stdout, _stderr));
  }
}

class AlwaysAllowChecklist implements Checklist {
  @override
  Future<bool> isDeviceReady(List<TestBundle> bundles) => Future.value(true);
}
