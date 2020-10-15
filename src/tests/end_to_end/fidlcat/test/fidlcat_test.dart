// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' show Directory, File, Platform, Process, ProcessResult;

import 'package:logging/logging.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 5));

enum RunMode { withAgent, withoutAgent }

void printErrorHelp() {
  print('If this test fails, see '
      'https://fuchsia.googlesource.com/a/fuchsia/+/master/src/tests/end_to_end/fidlcat/README.md'
      ' for details!');
}

/// Formats an IP address so that fidlcat can understand it (removes % part,
/// adds brackets around it.)
String formatTarget(Logger log, String target) {
  log.info('$target: target');
  try {
    Uri.parseIPv4Address(target);
    return target;
  } on FormatException {
    try {
      Uri.parseIPv6Address(target);
      return '[$target]';
    } on FormatException {
      try {
        Uri.parseIPv6Address(target.split('%')[0]);
        return '[$target]';
      } on FormatException {
        return null;
      }
    }
  }
}

class RunFidlcat {
  String target;
  int port;
  Future<ProcessResult> agentResult;
  String stdout;
  String stderr;
  String additionalResult;

  Future<void> run(Logger log, sl4f.Sl4f sl4fDriver, String path,
      RunMode runMode, List<String> extraArguments) async {
    if (runMode == RunMode.withAgent) {
      port = await sl4fDriver.ssh.pickUnusedPort();
      log.info('Chose port: $port');

      /// fuchsia-pkg URL for the debug agent.
      const String debugAgentUrl =
          'fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx';

      agentResult = sl4fDriver.ssh.run('run $debugAgentUrl --port=$port');
      target = formatTarget(log, sl4fDriver.ssh.target);
      log.info('Target: $target');
    }

    List<String> arguments;
    final String symbolPath = Platform.script
        .resolve('runtime_deps/echo_client_cpp.debug')
        .toFilePath();
    // We have to list all of the IR we need explicitly, here and in the BUILD.gn file. The
    // two lists must be kept in sync: if you add an IR here, you must also add it to the
    // BUILD.gn file.
    final String echoIr =
        Platform.script.resolve('runtime_deps/echo.fidl.json').toFilePath();
    final String ioIr = Platform.script
        .resolve('runtime_deps/fuchsia.io.fidl.json')
        .toFilePath();
    final String sysIr = Platform.script
        .resolve('runtime_deps/fuchsia.sys.fidl.json')
        .toFilePath();
    arguments = [
      '--fidl-ir-path=$echoIr',
      '--fidl-ir-path=$ioIr',
      '--fidl-ir-path=$sysIr',
      '-s',
      '$symbolPath',
    ]..addAll(extraArguments);
    if (runMode == RunMode.withAgent) {
      arguments =
          ['--connect=$target:$port', '--quit-agent-on-exit'] + arguments;
    }
    ProcessResult processResult;
    do {
      processResult = await Process.run(path, arguments);
    } while (processResult.exitCode == 2); // 2 means can't connect (yet).

    stdout = processResult.stdout.toString();
    stderr = processResult.stderr.toString();
    additionalResult = 'stderr ===\n$stderr\nstdout ===\n$stdout';
  }
}

void main(List<String> arguments) {
  final log = Logger('fidlcat_test');

  /// Location of the fidlcat executable.
  final String fidlcatPath =
      Platform.script.resolve('runtime_deps/fidlcat').toFilePath();

  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  tearDownAll(printErrorHelp);

  /// Simple test to ensure that fidlcat can run the echo client, and that some of the expected
  /// output is present.  It starts the agent on the target, and then launches fidlcat with the
  /// correct parameters.
  group('fidlcat', () {
    test('Simple test of echo client output and shutdown', () async {
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withAgent, [
        'run',
        'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx'
      ]);

      expect(
          instance.stdout,
          contains('sent request fidl.examples.echo/Echo.EchoString = {\n'
              '    value: string = "hello world"\n'
              '  }'),
          reason: instance.additionalResult);

      await instance.agentResult;
    });

    test('Test --extra-name', () async {
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withAgent, [
        '--remote-name=echo_server',
        '--extra-name=echo_client',
        'run',
        'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx'
      ]);

      final lines = instance.stdout.split('\n\n');

      /// If we had use --remote-name twice, we would have a lot of messages between
      /// "Monitoring echo_client" and "Monitoring echo_server".
      /// With --extra-name for echo_client, we wait for echo_server before monitoring echo_client.
      /// Therefore, both line are one after the other.
      expect(lines[1], contains('Monitoring echo_client_cpp.cmx koid='),
          reason: instance.additionalResult);

      expect(lines[2], contains('Monitoring echo_server_cpp.cmx koid='),
          reason: instance.additionalResult);

      await instance.agentResult;
    });

    test('Test --trigger', () async {
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withAgent, [
        '--trigger=.*EchoString',
        'run',
        'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx'
      ]);

      final lines = instance.stdout.split('\n\n');

      /// The first displayed message must be EchoString.
      expect(lines[2],
          contains('sent request fidl.examples.echo/Echo.EchoString = {\n'),
          reason: instance.additionalResult);

      await instance.agentResult;
    });

    test('Test --messages', () async {
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withAgent, [
        '--messages=.*EchoString',
        '--exclude-syscalls=zx_channel_create',
        'run',
        'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx'
      ]);

      final lines = instance.stdout.split('\n\n');

      /// The first and second displayed messages must be EchoString (everything else has been
      /// filtered out).
      expect(
          lines[4],
          contains('sent request fidl.examples.echo/Echo.EchoString = {\n'
              '    value: string = "hello world"\n'
              '  }'),
          reason: instance.additionalResult);
      expect(
          lines[5],
          contains('received response fidl.examples.echo/Echo.EchoString = {\n'
              '      response: string = "hello world"\n'
              '    }'),
          reason: instance.additionalResult);

      await instance.agentResult;
    });

    test('Test save/replay', () async {
      var systemTempDir = Directory.systemTemp;
      var fidlcatTemp = systemTempDir.createTempSync('fidlcat');
      final String savePath = '${fidlcatTemp.path}/save.proto';

      var instanceSave = RunFidlcat();
      await instanceSave.run(log, sl4fDriver, fidlcatPath, RunMode.withAgent, [
        '--to',
        savePath,
        'run',
        'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx'
      ]);

      expect(
          instanceSave.stdout,
          contains('sent request fidl.examples.echo/Echo.EchoString = {\n'
              '    value: string = "hello world"\n'
              '  }'),
          reason: instanceSave.additionalResult);

      await instanceSave.agentResult;

      var instanceReplay = RunFidlcat();
      await instanceReplay.run(log, sl4fDriver, fidlcatPath,
          RunMode.withoutAgent, ['--from', savePath]);

      expect(
          instanceReplay.stdout,
          contains('sent request fidl.examples.echo/Echo.EchoString = {\n'
              '    value: string = "hello world"\n'
              '  }'),
          reason: instanceReplay.additionalResult);
    });

    test('Test --with=generate-tests (more than one proces)', () async {
      final String echoProto =
          Platform.script.resolve('runtime_deps/echo.proto').toFilePath();

      var systemTempDir = Directory.systemTemp;
      var fidlcatTemp = systemTempDir.createTempSync('fidlcat-extracted-tests');

      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent,
          ['--with=generate-tests=${fidlcatTemp.path}', '--from=$echoProto']);

      expect(
          instance.stdout,
          equals('Error: Cannot generate tests for more than one process.\n'
              ''),
          reason: instance.additionalResult);
    });

    test('Test --with=generate-tests', () async {
      final String echoClientProto = Platform.script
          .resolve('runtime_deps/echo_client.proto')
          .toFilePath();

      var systemTempDir = Directory.systemTemp;
      var fidlcatTemp = systemTempDir.createTempSync('fidlcat-extracted-tests');

      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent, [
        '--with=generate-tests=${fidlcatTemp.path}',
        '--from=$echoClientProto'
      ]);

      expect(
          instance.stdout,
          equals('Writing tests on disk\n'
              '  process name: echo_client_cpp\n'
              '  output directory: "${fidlcatTemp.path}"\n'
              '1412899975 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_0.cc"\n'
              '\n'
              '1416045099 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_1.cc"\n'
              '\n'
              '1428628083 zx_channel_write fidl.examples.echo/Echo.EchoString\n'
              '1428628083 zx_channel_read fidl.examples.echo/Echo.EchoString\n'
              '... Writing to "${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc"\n'
              '\n'
              '1430725227 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_2.cc"\n'
              '\n'
              '1435967747 zx_channel_write fuchsia.io/Node.OnOpen\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__node_0.cc"\n'
              '\n'
              '1457988959 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_sys__launcher_0.cc"\n'
              '\n'
              '1466376519 zx_channel_read fuchsia.sys/ComponentController.OnDirectoryReady\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_sys__component_controller_0.cc"\n'
              '\n'
              '1492595047 zx_channel_read fuchsia.io/Node.Clone\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__node_1.cc"\n'
              '\n'
              ''),
          reason: instance.additionalResult);

      // Checks that files exist on disk
      expect(
          File('${fidlcatTemp.path}/fuchsia_io__directory_0.cc').existsSync(),
          isTrue);
      expect(
          File('${fidlcatTemp.path}/fuchsia_io__directory_1.cc').existsSync(),
          isTrue);
      expect(
          File('${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc')
              .existsSync(),
          isTrue);
      expect(
          File('${fidlcatTemp.path}/fuchsia_io__directory_2.cc').existsSync(),
          isTrue);
      expect(File('${fidlcatTemp.path}/fuchsia_io__node_0.cc').existsSync(),
          isTrue);
      expect(
          File('${fidlcatTemp.path}/fuchsia_sys__launcher_0.cc').existsSync(),
          isTrue);
      expect(
          File('${fidlcatTemp.path}/fuchsia_sys__component_controller_0.cc')
              .existsSync(),
          isTrue);
      expect(File('${fidlcatTemp.path}/fuchsia_io__node_1.cc').existsSync(),
          isTrue);

      // Checks that the generated code is identical to the golden file
      expect(
          File('${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc')
              .readAsStringSync(),
          equals(File(Platform.script
                  .resolve(
                      'runtime_deps/fidl_examples_echo__echo.test.cc.golden')
                  .toFilePath())
              .readAsStringSync()));
    });

    test('Test --with=generate-tests (sync)', () async {
      final String echoClientSyncProto = Platform.script
          .resolve('runtime_deps/echo_client_sync.proto')
          .toFilePath();

      var systemTempDir = Directory.systemTemp;
      var fidlcatTemp = systemTempDir.createTempSync('fidlcat-extracted-tests');

      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent, [
        '--with=generate-tests=${fidlcatTemp.path}',
        '--from=$echoClientSyncProto'
      ]);

      expect(
          instance.stdout,
          equals('Writing tests on disk\n'
              '  process name: echo_client_cpp_synchronous\n'
              '  output directory: "${fidlcatTemp.path}"\n'
              '1662590155 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_0.cc"\n'
              '\n'
              '1722359527 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_1.cc"\n'
              '\n'
              '1950948475 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_sys__launcher_0.cc"\n'
              '\n'
              '2009669511 zx_channel_call fidl.examples.echo/Echo.EchoString\n'
              '... Writing to "${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc"\n'
              '\n'
              '2085165403 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_2.cc"\n'
              '\n'
              ''),
          reason: instance.additionalResult);

      // Checks that the generated code is identical to the golden file
      expect(
          File('${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc')
              .readAsStringSync(),
          equals(File(Platform.script
                  .resolve(
                      'runtime_deps/fidl_examples_echo__echo_sync.test.cc.golden')
                  .toFilePath())
              .readAsStringSync()));
    });

    test('Test --with=generate-tests (server crashing)', () async {
      final String echoCrashProto = Platform.script
          .resolve('runtime_deps/echo_sync_crash.proto')
          .toFilePath();

      var systemTempDir = Directory.systemTemp;
      var fidlcatTemp = systemTempDir.createTempSync('fidlcat-extracted-tests');

      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent, [
        '--with=generate-tests=${fidlcatTemp.path}',
        '--from=$echoCrashProto'
      ]);

      expect(
          instance.stdout,
          equals('Writing tests on disk\n'
              '  process name: echo_client_cpp_synchronous\n'
              '  output directory: "${fidlcatTemp.path}"\n'
              '1150113659 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_sys__launcher_0.cc"\n'
              '\n'
              '2223856655 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_0.cc"\n'
              '\n'
              '2224905275 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_1.cc"\n'
              '\n'
              '2243779711 zx_channel_write fuchsia.io/Directory.Open\n'
              '... Writing to "${fidlcatTemp.path}/fuchsia_io__directory_2.cc"\n'
              '\n'
              '2674743383 zx_channel_call (crashed) fidl.examples.echo/Echo.EchoString\n'
              '... Writing to "${fidlcatTemp.path}/fidl_examples_echo__echo_0.cc"\n'
              '\n'
              ''),
          reason: instance.additionalResult);
    });

    test('Test --with=summary', () async {
      final String echoProto =
          Platform.script.resolve('runtime_deps/echo.proto').toFilePath();
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent,
          ['--with=summary', '--from=$echoProto']);

      expect(
          instance.stdout,
          equals(
              '--------------------------------------------------------------------------------'
              'echo_client_cpp.cmx 26251: 26 handles\n'
              '\n'
              '  Process:4cd5cb37(proc-self)\n'
              '\n'
              '  startup Vmar:4cd5cb3b(vmar-root)\n'
              '\n'
              '  startup Thread:4cd5cb3f(thread-self)\n'
              '\n'
              '  startup Channel:4c25cb57(dir:/pkg)\n'
              '\n'
              '  startup Channel:4cb5cb07(dir:/svc)\n'
              '      write request  fuchsia.io/Directory.Open(".")\n'
              '        -> Channel:4db5cb4f(dir:/svc)\n'
              '\n'
              '  startup Job:4cc5cb17(job-default)\n'
              '\n'
              '  startup Channel:4c85cb0f(directory-request:/)\n'
              '      read  request  fuchsia.io/Node.Clone\n'
              '      read  request  fuchsia.io/Directory.Open("diagnostics")\n'
              '        -> Channel:4df5f45f(directory-request:/diagnostics)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  startup Socket:4cd5cb23(fd:0)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  startup Socket:4ce5caab(fd:1)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  startup Socket:4ce5cab3(fd:2)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  startup Vmo:4cb5cbd7(vdso-vmo)\n'
              '\n'
              '  startup Vmo:4cc5cbdf(stack-vmo)\n'
              '\n'
              '  Channel:4cb5cb93(channel:0)\n'
              '    linked to Channel:4db5cb4f(dir:/svc)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:4cb5cb07(dir:/svc) sending fuchsia.io/Directory.Open\n'
              '\n'
              '  Channel:4db5cb4f(dir:/svc)\n'
              '    linked to Channel:4cb5cb93(channel:0)\n'
              '    created by zx_channel_create\n'
              '      write request  fuchsia.io/Directory.Open("fuchsia.sys.Launcher")\n'
              '        -> Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4cc5cba7('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
              '    linked to Channel:4cb5cba3(channel:3)\n'
              '    which is  Channel:83cadf63(directory-request:/svc)'
              ' in process echo_server_cpp.cmx:26568\n'
              '    created by zx_channel_create\n'
              '      write request  fuchsia.io/Directory.Open("fidl.examples.echo.Echo")\n'
              '        -> Channel:4c85f443('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/'
              'fidl.examples.echo.Echo)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4cb5cba3(channel:3)\n'
              '    linked to Channel:4cc5cba7('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)'
              ' sending fuchsia.sys/Launcher.CreateComponent\n'
              '\n'
              '  Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
              '    linked to Channel:4ca5cbaf(channel:5)\n'
              '    created by zx_channel_create\n'
              '      write request  fuchsia.sys/Launcher.CreateComponent\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4ca5cbaf(channel:5)\n'
              '    linked to Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:4db5cb4f(dir:/svc) sending fuchsia.io/Directory.Open\n'
              '\n'
              '  Channel:4c65cbb3(server-control:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
              '    linked to Channel:4c65cbb7(channel:7)\n'
              '    created by zx_channel_create\n'
              '      read  event    fuchsia.sys/ComponentController.OnDirectoryReady\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4c65cbb7(channel:7)\n'
              '    linked to Channel:4c65cbb3(server-control:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)'
              ' sending fuchsia.sys/Launcher.CreateComponent\n'
              '\n'
              '  Channel:4c85f443(server:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo)\n'
              '    linked to Channel:4c45cbbb(channel:9)\n'
              '    which is  Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo)'
              ' in process echo_server_cpp.cmx:26568\n'
              '    created by zx_channel_create\n'
              '      write request  fidl.examples.echo/Echo.EchoString\n'
              '      read  response fidl.examples.echo/Echo.EchoString\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4c45cbbb(channel:9)\n'
              '    linked to Channel:4c85f443(server:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:4cc5cba7(server:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)'
              ' sending fuchsia.io/Directory.Open\n'
              '\n'
              '  Channel:4cc5cb2f(directory-request:/)\n'
              '    created by Channel:4c85cb0f(directory-request:/)'
              ' receiving fuchsia.io/Node.Clone\n'
              '      write event    fuchsia.io/Node.OnOpen\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:4df5f45f(directory-request:/diagnostics)\n'
              '    created by Channel:4c85cb0f(directory-request:/)'
              ' receiving fuchsia.io/Directory.Open\n'
              '      write event    fuchsia.io/Node.OnOpen\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Port:4cb5cb8f()\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Timer:4c85cb8b()\n'
              '    closed by zx_handle_close\n'
              '\n'
              '--------------------------------------------------------------------------------'
              'echo_server_cpp.cmx 26568: 18 handles\n'
              '\n'
              '  Process:839ae00b(proc-self)\n'
              '\n'
              '  startup Vmar:839ae00f(vmar-root)\n'
              '\n'
              '  startup Thread:839ae013(thread-self)\n'
              '\n'
              '  startup Channel:834ae0b7(dir:/pkg)\n'
              '\n'
              '  startup Channel:830ae0cb(dir:/svc)\n'
              '      write request  fuchsia.io/Directory.Open(".")\n'
              '        -> Channel:831ae04b(dir:/svc)\n'
              '\n'
              '  startup Job:839ae0df(job-default)\n'
              '\n'
              '  startup Channel:839ae0d7(directory-request:/)\n'
              '      read  request  fuchsia.io/Directory.Open("svc")\n'
              '        -> Channel:83cadf63(directory-request:/svc)\n'
              '      read  request  fuchsia.io/Node.Clone\n'
              '      read  request  fuchsia.io/Directory.Open("diagnostics")\n'
              '        -> Channel:83aae067(directory-request:/diagnostics)\n'
              '\n'
              '  startup Socket:839ae0ef(fd:0)\n'
              '\n'
              '  startup Log:839ae0f3(fd:1)\n'
              '\n'
              '  startup Log:839ae0f7(fd:2)\n'
              '\n'
              '  startup Vmo:83bae027(vdso-vmo)\n'
              '\n'
              '  startup Vmo:83aae02f(stack-vmo)\n'
              '\n'
              '  Channel:83dae053(channel:10)\n'
              '    linked to Channel:831ae04b(dir:/svc)\n'
              '    created by zx_channel_create\n'
              '    closed by Channel:830ae0cb(dir:/svc) sending fuchsia.io/Directory.Open\n'
              '\n'
              '  Channel:831ae04b(dir:/svc)\n'
              '    linked to Channel:83dae053(channel:10)\n'
              '    created by zx_channel_create\n'
              '\n'
              '  Channel:83cadf63(directory-request:/svc)\n'
              '    linked to Channel:4cc5cba7('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)'
              ' in process echo_client_cpp.cmx:26251\n'
              '    created by Channel:839ae0d7(directory-request:/)'
              ' receiving fuchsia.io/Directory.Open\n'
              '      read  request  fuchsia.io/Directory.Open("fidl.examples.echo.Echo")\n'
              '        -> Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo)\n'
              '    closed by zx_handle_close\n'
              '\n'
              '  Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo)\n'
              '    linked to Channel:4c85f443(server:fuchsia-pkg:'
              '//fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo)'
              ' in process echo_client_cpp.cmx:26251\n'
              '    created by Channel:83cadf63(directory-request:/svc)'
              ' receiving fuchsia.io/Directory.Open\n'
              '      read  request  fidl.examples.echo/Echo.EchoString\n'
              '      write response fidl.examples.echo/Echo.EchoString\n'
              '\n'
              '  Channel:83aae003(directory-request:/)\n'
              '    created by Channel:839ae0d7(directory-request:/)'
              ' receiving fuchsia.io/Node.Clone\n'
              '      write event    fuchsia.io/Node.OnOpen\n'
              '\n'
              '  Channel:83aae067(directory-request:/diagnostics)\n'
              '    created by Channel:839ae0d7(directory-request:/)'
              ' receiving fuchsia.io/Directory.Open\n'
              ''),
          reason: instance.additionalResult);
    });

    test('Test --with=top', () async {
      final String echoProto =
          Platform.script.resolve('runtime_deps/echo.proto').toFilePath();
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent,
          ['--with=top', '--from=$echoProto']);

      expect(
          instance.stdout,
          equals(
              '--------------------------------------------------------------------------------'
              'echo_client_cpp.cmx 26251: 11 events\n'
              '  fuchsia.io/Directory: 4 events\n'
              '    Open: 4 events\n'
              '      write request  fuchsia.io/Directory.Open(Channel:4cb5cb07(dir:/svc), ".")\n'
              '        -> Channel:4db5cb4f(dir:/svc)\n'
              '      write request  fuchsia.io/Directory.Open(Channel:4db5cb4f(dir:/svc),'
              ' "fuchsia.sys.Launcher")\n'
              '        -> Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
              '      write request  fuchsia.io/Directory.Open(Channel:4cc5cba7('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx),'
              ' "fidl.examples.echo.Echo")\n'
              '        -> Channel:4c85f443('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/'
              'fidl.examples.echo.Echo)\n'
              '      read  request  fuchsia.io/Directory.Open(Channel:4c85cb0f('
              'directory-request:/), "diagnostics")\n'
              '        -> Channel:4df5f45f(directory-request:/diagnostics)\n'
              '\n'
              '  fuchsia.io/Node: 3 events\n'
              '    OnOpen: 2 events\n'
              '      write event    fuchsia.io/Node.OnOpen(Channel:4cc5cb2f('
              'directory-request:/))\n'
              '      write event    fuchsia.io/Node.OnOpen(Channel:4df5f45f('
              'directory-request:/diagnostics))\n'
              '    Clone: 1 event\n'
              '      read  request  fuchsia.io/Node.Clone(Channel:4c85cb0f(directory-request:/))\n'
              '\n'
              '  fidl.examples.echo/Echo: 2 events\n'
              '    EchoString: 2 events\n'
              '      write request  fidl.examples.echo/Echo.EchoString(Channel:4c85f443('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp'
              '#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo))\n'
              '      read  response fidl.examples.echo/Echo.EchoString(Channel:4c85f443('
              'server:fuchsia-pkg://fuchsia.com/echo_server_cpp#'
              'meta/echo_server_cpp.cmx/fidl.examples.echo.Echo))\n'
              '\n'
              '  fuchsia.sys/ComponentController: 1 event\n'
              '    OnDirectoryReady: 1 event\n'
              '      read  event    fuchsia.sys/ComponentController.OnDirectoryReady('
              'Channel:4c65cbb3('
              'server-control:fuchsia-pkg://fuchsia.com/echo_server_cpp'
              '#meta/echo_server_cpp.cmx))\n'
              '\n'
              '  fuchsia.sys/Launcher: 1 event\n'
              '    CreateComponent: 1 event\n'
              '      write request  fuchsia.sys/Launcher.CreateComponent(Channel:4ca5cbab('
              'dir:/svc/fuchsia.sys.Launcher))\n'
              '\n'
              '--------------------------------------------------------------------------------'
              'echo_server_cpp.cmx 26568: 8 events\n'
              '  fuchsia.io/Directory: 4 events\n'
              '    Open: 4 events\n'
              '      write request  fuchsia.io/Directory.Open(Channel:830ae0cb(dir:/svc), ".")\n'
              '        -> Channel:831ae04b(dir:/svc)\n'
              '      read  request  fuchsia.io/Directory.Open(Channel:839ae0d7('
              'directory-request:/), "svc")\n'
              '        -> Channel:83cadf63(directory-request:/svc)\n'
              '      read  request  fuchsia.io/Directory.Open(Channel:83cadf63('
              'directory-request:/svc), "fidl.examples.echo.Echo")\n'
              '        -> Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo)\n'
              '      read  request  fuchsia.io/Directory.Open(Channel:839ae0d7('
              'directory-request:/), "diagnostics")\n'
              '        -> Channel:83aae067(directory-request:/diagnostics)\n'
              '\n'
              '  fidl.examples.echo/Echo: 2 events\n'
              '    EchoString: 2 events\n'
              '      read  request  fidl.examples.echo/Echo.EchoString(Channel:833adf7b('
              'directory-request:/svc/fidl.examples.echo.Echo))\n'
              '      write response fidl.examples.echo/Echo.EchoString(Channel:833adf7b('
              'directory-request:/svc/fidl.examples.echo.Echo))\n'
              '\n'
              '  fuchsia.io/Node: 2 events\n'
              '    Clone: 1 event\n'
              '      read  request  fuchsia.io/Node.Clone(Channel:839ae0d7(directory-request:/))\n'
              '    OnOpen: 1 event\n'
              '      write event    fuchsia.io/Node.OnOpen(Channel:83aae003('
              'directory-request:/))\n'),
          reason: instance.additionalResult);
    });

    test('Test --with=top and unknown message', () async {
      final String snapshotProto =
          Platform.script.resolve('runtime_deps/snapshot.proto').toFilePath();
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent,
          ['--with=top', '--from=$snapshotProto']);

      expect(
          instance.stdout,
          contains('  unknown interfaces: : 1 event\n'
              '      call   ordinal=36dadb5482dc1d55('
              'Channel:9b71d5c7(dir:/svc/fuchsia.feedback.DataProvider))\n'),
          reason: instance.additionalResult);
    });

    test('Test --with=messages and unknown message', () async {
      final String snapshotProto =
          Platform.script.resolve('runtime_deps/snapshot.proto').toFilePath();
      var instance = RunFidlcat();
      await instance.run(log, sl4fDriver, fidlcatPath, RunMode.withoutAgent,
          ['--messages=.*x.*', '--from=$snapshotProto']);

      /// We only check that fidlcat didn't crash.
      expect(instance.stdout,
          contains('Stop monitoring exceptions.cmx koid=19884\n'),
          reason: instance.additionalResult);
    });
  }, timeout: _timeout);
}
