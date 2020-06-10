import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('tests launcher allows clean for graceful component shutdown', () async {
    await sl4fDriver.ssh.run('killall "system_monitor_harvester.cmx"');
    await sl4f.Launch(sl4fDriver)
        .launch('system_monitor_harvester', ['--version']);
  }, timeout: Timeout.none);

  test('tests launcher with error', () async {
    final result = await sl4f.Launch(sl4fDriver).launch('fake');
    expect(result['Fail'], -1);
  }, timeout: Timeout.none);
}
