// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' show Platform, Process, ProcessResult;

import 'package:logging/logging.dart';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 5));

void main(List<String> arguments) {
  final log = Logger('fidlcat_test');

  /// fuchsia-pkg URL for the debug agent.
  const String debugAgentUrl =
      'fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx';

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

  /// Formats an IP address so that fidlcat can understand it (removes % part,
  /// adds brackets around it.)
  String formatTarget(String target) {
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

  /// Simple test to ensure that fidlcat can run the echo client, and that some of the expected
  /// output is present.  It starts the agent on the target, and then launches fidlcat with the
  /// correct parameters.
  group('fidlcat', () {
    test('Simple test of echo client output and shutdown', () async {
      int port = await sl4fDriver.ssh.pickUnusedPort();
      log.info('Chose port: $port');
      Future<ProcessResult> agentResult =
          sl4fDriver.ssh.run('run $debugAgentUrl --port=$port');
      String target = formatTarget(sl4fDriver.ssh.target);
      log.info('Target: $target');

      final String symbolPath = Platform.script
          .resolve('runtime_deps/echo_client_cpp.debug')
          .toFilePath();
      // We have to list all of the IR we need explicitly, here and in the BUILD.gn file. The
      // two lists must be kept in sync: if you add an IR here, you must also add it to the
      // BUILD.gn file.
      final String echoIr =
          Platform.script.resolve('runtime_deps/echo.fidl.json').toFilePath();
      final String ioIr = Platform.script
          .resolve('runtime_deps/fuchsia-io.fidl.json')
          .toFilePath();
      final String sysIr = Platform.script
          .resolve('runtime_deps/fuchsia.sys.fidl.json')
          .toFilePath();
      ProcessResult processResult;
      do {
        processResult = await Process.run(fidlcatPath, [
          '--connect=$target:$port',
          '--quit-agent-on-exit',
          '--fidl-ir-path=$echoIr',
          '--fidl-ir-path=$ioIr',
          '--fidl-ir-path=$sysIr',
          '-s',
          '$symbolPath',
          'run',
          'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx',
        ]);
      } while (processResult.exitCode == 2); // 2 means can't connect (yet).
      String additionalResult =
          'stderr ===\n${processResult.stderr.toString()}\nstdout ===\n${processResult.stdout.toString()}';
      expect(
          processResult.stdout.toString(),
          contains(
              'sent request fidl.examples.echo/Echo.EchoString = {"value":"hello world"}'),
          reason: additionalResult);
      await agentResult;
    });
  }, timeout: _timeout);
}
