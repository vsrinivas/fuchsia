// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:path/path.dart' as path;
import 'package:test/test.dart';

const String ipv4Env = 'FUCHSIA_IPV4_ADDR';
const String outputEnv = 'FUCHSIA_TEST_OUTDIR';

const int deviceReadyTimeoutSec = 30;

// Configuration file used by ACTS. See format here:
// https://g3doc.corp.google.com/third_party/py/acts/README.md#configuration-files.
const String actsConfigAbsPath = '/etc/connectivity/acts_config.json';
const String actsRelativePath =
    'host_x64/gen/garnet/tests/acts_tests/acts/acts.zip';
const String pythonRelativePath =
    '../../prebuilt/third_party/python3/linux-x64/bin/python3.8';
const String virtualEnvName = 'my_virtualenv';

Future<void> waitDeviceReady(final String ip) async {
  for (var i = 0; i < deviceReadyTimeoutSec; i++) {
    final result = Process.runSync('ping', [ip, '-c', '1', '-W', '1']);
    if (result.exitCode == 0) {
      print('Fuchsia device is online @ [$ip].');
      return;
    }
    await Future.delayed(const Duration(seconds: 1));
  }
  throw Exception('Device [$ip] is not ping-able.');
}

void setupActs(final String tmpDirPath, final String tests) {
  final actsPath = path.join(Directory.current.path, actsRelativePath);

  // Install virtualenv
  var installVirtualEnv = Process.runSync(
      '$pythonRelativePath', ['-m', 'pip', 'install', 'virtualenv']);
  stdout.write(installVirtualEnv.stdout);
  stderr.write(installVirtualEnv.stderr);

  // Create the virtual environment
  var virtualEnv = Process.runSync('$pythonRelativePath',
      ['-m', 'virtualenv', '$tmpDirPath/$virtualEnvName']);
  stdout.write(virtualEnv.stdout);
  stderr.write(virtualEnv.stderr);

  // Unzip ACTS under the virtual environment.
  Process.runSync(
      'unzip', [actsPath, '-d', '$tmpDirPath/$virtualEnvName/acts']);

  // Run virtualenv using python interpreter and install ACTS
  var setupActsEnv = Process.runSync('$tmpDirPath/$virtualEnvName/bin/python3',
      ['$tmpDirPath/$virtualEnvName/acts/setup.py', 'install']);
  stdout.write(setupActsEnv.stdout);
  stderr.write(setupActsEnv.stderr);
}

Future<Process> runActs(
    final String tmpDirPath, final String tests, final String outputDir) {
  final pythonPath = '$tmpDirPath/$virtualEnvName/bin/python3';
  final actsBinAbsPath =
      '$tmpDirPath/$virtualEnvName/acts/acts_framework/acts/bin/act.py';

  final actsArgs = [
    actsBinAbsPath,
    '-c',
    actsConfigAbsPath,
    '-lp',
    '$outputDir/acts_logpath',
    '-tc'
  ]..addAll(tests.split(' '));

  // Set ACTS binary as executable
  Process.runSync('chmod', ['+x', actsBinAbsPath]);

  return Process.start(pythonPath, actsArgs, environment: {
    'PYTHONPATH': '$tmpDirPath/$virtualEnvName/bin',
    'ACTS_LOGPATH': outputDir,
    // Required for some Wifi testcases
    'LANG': 'C.UTF-8',
    'LC_ALL': 'C.UTF-8'
  });
}

void main(final List<String> args) {
  final argResults = (ArgParser()
        ..addOption('tests',
            help: 'Space delimited string of ACTS tests to run.')
        ..addOption('timeout',
            help: 'Timeout in minutes for ACTS test execution.'))
      .parse(args);

  // Fetch env var
  final Map<String, String> envVars = Platform.environment;
  final String ipStr = envVars[ipv4Env];
  final String outputDir = envVars[outputEnv];

  test('ActsSuite', () async {
    final tmpDir = Directory.systemTemp.createTempSync('acts_');

    // Extract ACTS and set up virtual environment.
    setupActs(tmpDir.path, argResults['tests']);

    // Wait for device to be online before running tests
    for (var ip in ipStr.split(',')) {
      await waitDeviceReady(ip);
    }

    // Run ACTS tests
    final proc = await runActs(tmpDir.path, argResults['tests'], outputDir);
    await Future.wait(
        [stdout.addStream(proc.stdout), stderr.addStream(proc.stderr)]);
    expect(await proc.exitCode, equals(0));
  }, timeout: Timeout(Duration(minutes: int.parse(argResults['timeout']))));
}
