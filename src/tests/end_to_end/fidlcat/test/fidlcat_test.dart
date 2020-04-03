// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io'
    show
        Directory,
        File,
        FileMode,
        FileSystemException,
        Platform,
        Process,
        ProcessResult;

import 'package:args/args.dart';
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

  /// Location of the IR for the core SDK APIs.
  String fidlIrPath;

  /// A convenient directory for temporary files
  Directory tempDir;

  /// The core.fidl_json.txt file passed to this program contains a list of
  /// FIDL IR files relative to the root build directory.  This test program
  /// is not run out of the root build directory, so we create a new file
  /// containing absolute paths.
  void setPath() {
    final parser = ArgParser()..addOption('data-dir');
    final argResults = parser.parse(arguments);
    final String dataDir = argResults['data-dir'];

    tempDir = Directory.systemTemp.createTempSync();

    final int timestamp = (DateTime.now()).microsecondsSinceEpoch;
    final String tempPath = tempDir.path;
    fidlIrPath = '$tempPath/core.fidl_json_processed-$timestamp.txt';

    final outFile = File(fidlIrPath)..createSync(recursive: true);

    final String fidlIrPathInit =
        Platform.script.resolve('runtime_deps/core.fidl_json.txt').toFilePath();
    File(fidlIrPathInit)
        .openRead()
        .transform(utf8.decoder)
        .transform(LineSplitter())
        .forEach((line) => {
              outFile.writeAsStringSync('$dataDir/$line\n',
                  mode: FileMode.append)
            });
  }

  void cleanup() {
    try {
      tempDir.deleteSync(recursive: true);
    } on FileSystemException {
      // Do nothing.
    }
  }

  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    setPath();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
    cleanup();
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
      final String echoIr =
          Platform.script.resolve('runtime_deps/echo.fidl.json').toFilePath();
      ProcessResult processResult;
      do {
        processResult = await Process.run(fidlcatPath, [
          '--connect=$target:$port',
          '--quit-agent-on-exit',
          '--fidl-ir-path=@$fidlIrPath',
          '--fidl-ir-path=$echoIr',
          '-s',
          '$symbolPath',
          'run',
          'fuchsia-pkg://fuchsia.com/echo_client_cpp#meta/echo_client_cpp.cmx',
        ]);
      } while (processResult.exitCode == 2); // 2 means can't connect (yet).
      expect(
          processResult.stdout.toString(),
          contains(
              'sent request fidl.examples.echo/Echo.EchoString = {"value":"hello world"}'));
      await agentResult;
    });
  }, timeout: _timeout);
}
