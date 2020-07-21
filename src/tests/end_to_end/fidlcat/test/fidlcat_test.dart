// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' show Directory, Platform, Process, ProcessResult;

import 'package:logging/logging.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 5));

enum RunMode { withAgent, withoutAgent }

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
  }, timeout: _timeout);
}
