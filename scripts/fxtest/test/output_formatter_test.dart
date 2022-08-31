import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:test/test.dart';
import 'fake_fx_env.dart';

void main() {
  var config = TestsConfig.fromRawArgs(
    rawArgs: [],
    fxEnv: FakeFxEnv.shared,
  );

  test('standard output formatter displays ratio correctly', () {
    var formatter = StandardOutputFormatter(
        hasRealTimeOutput: false, wrapWith: config.wrapWith);
    expect(formatter.ratioDisplay,
        '${darkGray.escape}PASS: 0${resetAll.escape} ${darkGray.escape}FAIL: 0${resetAll.escape}');
  });

  test('info formatter displays ratio correctly', () {
    var formatter = InfoFormatter();
    expect(formatter.ratioDisplay, 'PASS: 0 FAIL: 0');
  });

  test('standard output formatter always displays test preprocessing errors',
      () {
    final buffer = OutputBuffer.locMemIO();
    StandardOutputFormatter(
            hasRealTimeOutput: true, wrapWith: config.wrapWith, buffer: buffer)
        .update(TestResult.failedPreprocessing(
            testName: 'my_test', message: 'what is a test'));
    var allOutput = buffer.content.join('\n');
    expect(allOutput, contains('what is a test'));
  });
}
