// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:mockito/mockito.dart';
import 'package:status/status.dart';
import 'package:test/test.dart';

// Mock this because it reads a real life file on disk
class MockDeviceFilenameReader extends Mock implements DeviceFilenameReader {}

// Mock this because it talks to .git
class MockGitStatusChecker extends Mock implements GitStatusChecker {}

// Mock this because it calls `fx gn ...`
class MockGNStatusChecker extends Mock implements GNStatusChecker {}

// Mock this because it checks environment variables
class MockEnvReader extends Mock implements EnvReader {}

void main() {
  group('EnvironmentCollector', () {
    test('returns default when deviceName file is empty', () async {
      // Not setting a return value for `getDeviceName()` means it returns None,
      // simulating the Collector not finding a device name file
      MockDeviceFilenameReader filenameReader = MockDeviceFilenameReader();
      EnvironmentCollector env = EnvironmentCollector();
      List<Item> results = await env.collect(filenameReader: filenameReader);
      expect(results.length, 1);
    });
    test('returns specific result when deviceName file is found', () async {
      MockDeviceFilenameReader filenameReader = MockDeviceFilenameReader();
      EnvironmentCollector env = EnvironmentCollector();
      // Setting this return value mimics finding a device name file
      when(filenameReader.getDeviceName(envReader: EnvReader.shared))
          .thenReturn('asdf');
      List<Item> results = await env.collect(filenameReader: filenameReader);
      expect(results.length, 2);
      expect(results[1].key, 'device_name');
      expect(results[1].title, 'Device name');
      expect(results[1].value, 'asdf');
      expect(results[1].notes, 'set by `fx set-device`');
    });
    test('returns env result even when deviceName file is present', () async {
      MockEnvReader envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DEVICE_NAME'))
          .thenReturn('Really good device');
      EnvironmentCollector env = EnvironmentCollector();
      List<Item> results = await env.collect(envReader: envReader);
      expect(results.length, 2);
      expect(results[1].key, 'device_name');
      expect(results[1].title, 'Device name');
      expect(results[1].value, 'Really good device');
      expect(results[1].notes, 'set by `fx -d`');
    });
  });

  group('GitCollector', () {
    test('returns True when git hashes are the same', () async {
      MockGitStatusChecker gitChecker = MockGitStatusChecker();
      when(gitChecker.checkStatus()).thenAnswer(
          (_) => Future.value(ProcessResult(123, 0, 'asdf\nasdf', '')));
      GitCollector gitCollector = GitCollector();
      List<Item> results =
          await gitCollector.collect(statusChecker: gitChecker);
      expect(results.length, 1);
      expect(results[0].value, true);
    });
    test('returns False when git hashes are different', () async {
      MockGitStatusChecker gitChecker = MockGitStatusChecker();
      when(gitChecker.checkStatus()).thenAnswer(
          (_) => Future.value(ProcessResult(123, 0, 'asdf\nnot-asdf', '')));
      GitCollector gitCollector = GitCollector();
      List<Item> results =
          await gitCollector.collect(statusChecker: gitChecker);
      expect(results.length, 1);
      expect(results[0].value, false);
    });

    test('returns Null when the git process has a non-zero exit code',
        () async {
      MockGitStatusChecker gitChecker = MockGitStatusChecker();
      when(gitChecker.checkStatus())
          .thenAnswer((_) => Future.value(ProcessResult(123, 1, '', '')));
      GitCollector gitCollector = GitCollector();
      List<Item> results =
          await gitCollector.collect(statusChecker: gitChecker);
      expect(results, null);
    });
  });

  /*
    Helper function to wrap up the process and callback binding tedium
    */
  Future<Map> runFxGn(argsGn) {
    var args = ['gn', 'format', '--dump-tree=json', '--stdin'];
    return Process.start('fx', args).then((Process pr) {
      pr.exitCode.then((exitCode) {
        if (exitCode != 0) {
          throw 'Unexpected error running fx gn format:\n' +
              'exit code $exitCode\n' +
              'command: fx $args\n' +
              'input:\n' +
              '$argsGn';
        }
      });
      // removes any text preceeding the actual json string. Since gn outputs
      // the json string to stderr, any warning output by fx, like the metrics
      // warning, will get in the way of the json, causing a parsing error.
      var data = pr.stderr
          .skipWhile((t) => t[0] != '{'.codeUnitAt(0))
          .transform(utf8.decoder)
          .map(jsonDecode)
          .cast<Map>()
          .first;
      pr.stdin
        ..writeln(argsGn)
        ..close();
      return data;
    });
  }

  /*
    Helper function to wrap up parsing the json data
    */
  Future<BasicGnParser> parseFxGn(argsGn) {
    return runFxGn(argsGn).then((data) => BasicGnParser(data['child']));
  }

  group('GNStatusParser', () {
    GNStatusParser parser = GNStatusParser();
    test('throws an error on non-zero exit codes', () async {
      expect(
        () => parser.parseGn(processResult: ProcessResult(123, 1, '', 'asdf')),
        throwsA(
            'Unexpected error running fx gn: exit code 1\n---- stderr output:\nasdf\n------'),
      );
    });
    test('parses a json that is preceded by a warning message', () async {
      var jsonWithText =
          '''WARNING: Please opt in or out of fx metrics collection.
             {"child": [
                {"type": "IDENTIFIER", "value": "use_goma"},
                {"type": "LITERAL", "value": "true"}
              ]}
      ''';
      ProcessResult pr = ProcessResult(123, 0, '', jsonWithText);
      List<Item> items = parser.parseGn(processResult: pr);
      expect(items.length, 2);
      expect(items[0].key, 'goma');
      expect(items[1].key, 'release');
    });
    test('handles variable assignments', () async {
      BasicGnParser parser =
          await parseFxGn('use_goma = true\nis_debug = false');
      expect(parser.assignedVariables['use_goma'], 'true');
      expect(parser.assignedVariables['is_debug'], 'false');
    });
    test('handles import statements', () async {
      BasicGnParser parser = await parseFxGn(
          'import("//products/core.gni")\nimport("//vendor/google/boards/x64.gni")');
      expect(parser.imports[0], '//products/core.gni');
      expect(parser.imports[1], '//vendor/google/boards/x64.gni');
    });
    test('correctly parses direct items', () async {
      BasicGnParser parser =
          await parseFxGn('universe_package_labels += [ "//bundles:tests" ]');
      var items = GNStatusParser()
          .collectFromTreeParser(parser)
          .where((item) => item.key == 'universe_package_labels');
      expect(items.length, 1);
      expect(items.first.key, 'universe_package_labels');
      expect(items.first.title, 'Universe packages');
      expect(items.first.value.length, 1);
      expect(items.first.value[0], '//bundles:tests');
    });
    test('correctly parses calculated variables', () async {
      BasicGnParser parser =
          await parseFxGn('use_goma = true\nis_debug = false');
      var items = GNStatusParser().collectFromTreeParser(parser);
      expect(items.length, 2);
      expect(items[0].key, 'goma');
      expect(items[0].title, 'Goma');
      expect(items[0].value, 'enabled');
      expect(items[1].key, 'release');
      expect(items[1].title, 'Is release?');
      expect(items[1].value, 'true');
      expect(items[1].notes, '--release argument of `fx set`');
    });
  });

  group('fx gn format', () {
    test('parses partial files (useful for other tests)', () async {
      var data = await runFxGn('use_goma = true');
      expect(data, {
        'child': [
          {
            'child': [
              {'type': 'IDENTIFIER', 'value': 'use_goma'},
              {'type': 'LITERAL', 'value': 'true'}
            ],
            'type': 'BINARY',
            'value': '='
          }
        ],
        'type': 'BLOCK'
      });
    });

    test('parses well-formed gni file from stdin', () async {
      var data = await runFxGn('''import("//products/core.gni")
use_goma = true
goma_dir = "/Users/ldap/goma"

# See: fx args --list=base_package_labels
base_package_labels += []

# See: fx args --list=universe_package_labels
universe_package_labels += [
  "//third_party/dart-pkg/pub/io/",
]''');
      expect(data, {
        'child': [
          {
            'child': [
              {
                'child': [
                  {'type': 'LITERAL', 'value': '"//products/core.gni"'}
                ],
                'type': 'LIST'
              }
            ],
            'type': 'FUNCTION',
            'value': 'import'
          },
          {
            'child': [
              {'type': 'IDENTIFIER', 'value': 'use_goma'},
              {'type': 'LITERAL', 'value': 'true'}
            ],
            'type': 'BINARY',
            'value': '='
          },
          {
            'child': [
              {'type': 'IDENTIFIER', 'value': 'goma_dir'},
              {'type': 'LITERAL', 'value': '"/Users/ldap/goma"'}
            ],
            'type': 'BINARY',
            'value': '='
          },
          {
            'before_comment': ['# See: fx args --list=base_package_labels'],
            'child': [
              {'type': 'IDENTIFIER', 'value': 'base_package_labels'},
              {'child': [], 'type': 'LIST'}
            ],
            'type': 'BINARY',
            'value': '+='
          },
          {
            'before_comment': ['# See: fx args --list=universe_package_labels'],
            'child': [
              {'type': 'IDENTIFIER', 'value': 'universe_package_labels'},
              {
                'child': [
                  {
                    'type': 'LITERAL',
                    'value': '"//third_party/dart-pkg/pub/io/"'
                  }
                ],
                'type': 'LIST'
              }
            ],
            'type': 'BINARY',
            'value': '+='
          }
        ],
        'type': 'BLOCK'
      });
    });
  });
}
