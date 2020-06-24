import 'package:fxtest/fxtest.dart';
import 'package:fxtest/output/output_formatter.dart';
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
        '${green.escape}PASS: 0${resetAll.escape} ${red.escape}FAIL: 0${resetAll.escape}');
  });

  test('info formatter displays ratio correctly', () {
    var formatter = InfoFormatter();
    expect(formatter.ratioDisplay, 'PASS: 0 FAIL: 0');
  });
}
