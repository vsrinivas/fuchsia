// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
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
}
