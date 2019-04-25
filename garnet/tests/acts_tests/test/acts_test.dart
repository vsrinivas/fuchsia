// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:args/args.dart';
import 'package:test/test.dart';
import 'package:archive/archive.dart';


const String ipv4Env = 'FUCHSIA_IPV4_ADDR';
const String outputEnv = 'FUCHSIA_TEST_OUTDIR';

const int deviceReadyTimeoutSec = 30;

// TODO(chok): Use in-tree ACTS.zip which will likely be the CWD instead
const String actsZipAbsPath = '/etc/connectivity/acts.zip';
const String actsConfigAbsPath = '/etc/connectivity/acts_config.json';
const String actsBinRelPath = 'tools/test/connectivity/acts/framework/acts/bin/act.py';
const String actsFrameworkRelPath = 'tools/test/connectivity/acts/framework';
const String actsOutputPath = '/tmp/logs/tb/latest';
const String actsSummaryFileName = 'test_run_summary.json';

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

void extractActsZip(final String tmpDirPath) {
  final List<int> bytes = File(actsZipAbsPath).readAsBytesSync();
  final Archive archive = ZipDecoder().decodeBytes(bytes);

  // Extract the contents of the Zip archive to disk.
  for (ArchiveFile file in archive) {
    String filename = file.name;
    var filepath = '$tmpDirPath/$filename';
    if (file.isFile) {
      List<int> data = file.content;
      File(filepath)
        ..createSync(recursive: true)
        ..writeAsBytesSync(data);
    } else {
      Directory(filepath).create(recursive: true);
    }
  }
}

Future<Process> runActs(final String tmpDirPath, final String tests) {
  final actsBinAbsPath = '$tmpDirPath/$actsBinRelPath';

  final actsArgs = ['-c', actsConfigAbsPath, '-tc']
    ..addAll(tests.split(' '));

  // Set ACTS binary as executable
  Process.runSync('chmod', ['+x', actsBinAbsPath]);

  return Process.start(actsBinAbsPath, actsArgs, environment: {
    'PYTHONPATH': '$tmpDirPath/$actsFrameworkRelPath'
  });
}

void main(final List<String> args) {
  final argResults = (ArgParser()
        ..addOption('tests',
            help: 'Space delimited string of ACTS tests to run.'))
      .parse(args);

  // Fetch env var
  final Map<String, String> envVars = Platform.environment;
  final String ipStr = envVars[ipv4Env];
  final String outputDir = envVars[outputEnv];

  tearDown(() {
    // Copy ACTS output to designated dir for GCS upload
    Process.runSync('/bin/cp', ['-r', actsOutputPath, outputDir]);
  });

  test('ActsSuite', () async {
    // Extract ACTS zip into tmp dir
    final tmpDir = Directory.systemTemp.createTempSync('acts_');
    extractActsZip(tmpDir.path);

    // Wait for device to be online before running tests
    for(var ip in ipStr.split(',')) {
      await waitDeviceReady(ip);
    }

    // Run ACTS tests
    final proc = await runActs(tmpDir.path, argResults['tests']);
    await Future.wait([stdout.addStream(proc.stdout), stderr.addStream(proc.stderr)]);
    expect(await proc.exitCode, equals(0));

    // Assert that ACTS actually ran tests
    final json = File('$actsOutputPath/$actsSummaryFileName').readAsStringSync();
    final data = jsonDecode(json);
    expect(data, contains('Results'));
    expect(data['Summary']['Executed'], isNonZero);
  }, timeout: Timeout(Duration(minutes: 15)));
}
