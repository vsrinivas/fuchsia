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
      // With the `--stdin` option, `gn format --dump-tree` echos the formatted
      // GN right after the JSON data. We need to truncate this extra output so
      // that the JSON parser doesn't complain.
      var data = pr.stdout
          .transform(utf8.decoder)
          .transform(new LineSplitter())
          .takeWhile((s) => s != '}')
          .join('\n');
      pr.stdin
        ..writeln(argsGn)
        ..close();
      return data.then((d) => jsonDecode(d + '}'));
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
    test('parses a json output', () async {
      var jsonText = '''{"child": [
                {"type": "IDENTIFIER", "value": "use_goma"},
                {"type": "LITERAL", "value": "true"}
              ]}
      ''';
      ProcessResult pr = ProcessResult(123, 0, jsonText, '');
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
        'begin_token': '',
        'child': [
          {
            'child': [
              {
                'location': {
                  'begin_column': 1,
                  'begin_line': 1,
                  'end_column': 9,
                  'end_line': 1
                },
                'type': 'IDENTIFIER',
                'value': 'use_goma'
              },
              {
                'location': {
                  'begin_column': 12,
                  'begin_line': 1,
                  'end_column': 16,
                  'end_line': 1
                },
                'type': 'LITERAL',
                'value': 'true'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 1,
              'end_column': 16,
              'end_line': 1
            },
            'type': 'BINARY',
            'value': '='
          }
        ],
        'location': {
          'begin_column': 1,
          'begin_line': 1,
          'end_column': 16,
          'end_line': 1
        },
        'result_mode': 'discards_result',
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
        'begin_token': '',
        'child': [
          {
            'child': [
              {
                'begin_token': '(',
                'child': [
                  {
                    'location': {
                      'begin_column': 8,
                      'begin_line': 1,
                      'end_column': 29,
                      'end_line': 1
                    },
                    'type': 'LITERAL',
                    'value': '"//products/core.gni"'
                  }
                ],
                'end': {
                  'location': {
                    'begin_column': 29,
                    'begin_line': 1,
                    'end_column': 30,
                    'end_line': 1
                  },
                  'type': 'END',
                  'value': ')'
                },
                'location': {
                  'begin_column': 7,
                  'begin_line': 1,
                  'end_column': 29,
                  'end_line': 1
                },
                'type': 'LIST'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 1,
              'end_column': 29,
              'end_line': 1
            },
            'type': 'FUNCTION',
            'value': 'import'
          },
          {
            'child': [
              {
                'location': {
                  'begin_column': 1,
                  'begin_line': 2,
                  'end_column': 9,
                  'end_line': 2
                },
                'type': 'IDENTIFIER',
                'value': 'use_goma'
              },
              {
                'location': {
                  'begin_column': 12,
                  'begin_line': 2,
                  'end_column': 16,
                  'end_line': 2
                },
                'type': 'LITERAL',
                'value': 'true'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 2,
              'end_column': 16,
              'end_line': 2
            },
            'type': 'BINARY',
            'value': '='
          },
          {
            'child': [
              {
                'location': {
                  'begin_column': 1,
                  'begin_line': 3,
                  'end_column': 9,
                  'end_line': 3
                },
                'type': 'IDENTIFIER',
                'value': 'goma_dir'
              },
              {
                'location': {
                  'begin_column': 12,
                  'begin_line': 3,
                  'end_column': 30,
                  'end_line': 3
                },
                'type': 'LITERAL',
                'value': '"/Users/ldap/goma"'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 3,
              'end_column': 30,
              'end_line': 3
            },
            'type': 'BINARY',
            'value': '='
          },
          {
            'before_comment': ['# See: fx args --list=base_package_labels'],
            'child': [
              {
                'location': {
                  'begin_column': 1,
                  'begin_line': 6,
                  'end_column': 20,
                  'end_line': 6
                },
                'type': 'IDENTIFIER',
                'value': 'base_package_labels'
              },
              {
                'begin_token': '[',
                'child': [],
                'end': {
                  'location': {
                    'begin_column': 25,
                    'begin_line': 6,
                    'end_column': 26,
                    'end_line': 6
                  },
                  'type': 'END',
                  'value': ']'
                },
                'location': {
                  'begin_column': 24,
                  'begin_line': 6,
                  'end_column': 25,
                  'end_line': 6
                },
                'type': 'LIST'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 6,
              'end_column': 25,
              'end_line': 6
            },
            'type': 'BINARY',
            'value': '+='
          },
          {
            'before_comment': ['# See: fx args --list=universe_package_labels'],
            'child': [
              {
                'location': {
                  'begin_column': 1,
                  'begin_line': 9,
                  'end_column': 24,
                  'end_line': 9
                },
                'type': 'IDENTIFIER',
                'value': 'universe_package_labels'
              },
              {
                'begin_token': '[',
                'child': [
                  {
                    'location': {
                      'begin_column': 3,
                      'begin_line': 10,
                      'end_column': 35,
                      'end_line': 10
                    },
                    'type': 'LITERAL',
                    'value': '"//third_party/dart-pkg/pub/io/"'
                  }
                ],
                'end': {
                  'location': {
                    'begin_column': 1,
                    'begin_line': 11,
                    'end_column': 2,
                    'end_line': 11
                  },
                  'type': 'END',
                  'value': ']'
                },
                'location': {
                  'begin_column': 28,
                  'begin_line': 9,
                  'end_column': 1,
                  'end_line': 11
                },
                'type': 'LIST'
              }
            ],
            'location': {
              'begin_column': 1,
              'begin_line': 9,
              'end_column': 1,
              'end_line': 11
            },
            'type': 'BINARY',
            'value': '+='
          }
        ],
        'location': {
          'begin_column': 1,
          'begin_line': 1,
          'end_column': 1,
          'end_line': 11
        },
        'result_mode': 'discards_result',
        'type': 'BLOCK'
      });
    });
  });
}
