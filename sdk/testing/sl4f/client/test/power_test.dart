// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io' show File, Platform;

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockDump extends Mock implements Dump {}

class MockPerformance extends Mock implements Performance {}

class MockFile extends Mock implements File {}

final _fakeZedmonPath =
    Platform.script.resolve('runtime_deps/fake_zedmon').toFilePath();

const String _converterPath = '/path/to/catapult_converter';

// Paths to files that specify stderr and stdout for the fake_zedmon binary.
final _stderrPath = '$_fakeZedmonPath.stderr.testdata';
final _stdoutPath = '$_fakeZedmonPath.stdout.testdata';

/// Writes a file from the provided lines.
Future<void> _writeFile(String path, List<String> lines) async {
  final file = File(path);
  await file.create();
  final sink = file.openWrite();
  lines.forEach(sink.writeln);
  await sink.close();
}

/// Writes stderr and stdout data for the fake_zedmon process. stderr is
/// currently unused by this test, so it is always left empty.
Future<void> _setupTestData(List<String> stdoutLines) async {
  await Future.wait(<Future>[
    _writeFile(_stderrPath, <String>[]),
    _writeFile(_stdoutPath, stdoutLines)
  ]);
}

/// Waits until we have processed data from the fake_zedmon process up to the
/// specified data timestamp.
Future<void> _waitForZedmon(sl4f.Power power, int zedmonTimestamp) async {
  assert(zedmonTimestamp >= 0);
  for (;;) {
    await Future.delayed(Duration(milliseconds: 10));
    final timestamp = power.zedmonLatestTimestamp()?.inMicroseconds ?? -1;
    if (timestamp >= zedmonTimestamp) {
      return;
    }
  }
}

void _deleteTestData() {
  final stderrFile = File(_stderrPath);
  if (stderrFile.existsSync()) {
    stderrFile.deleteSync();
  }
  final stdoutFile = File(_stdoutPath);
  if (stdoutFile.existsSync()) {
    stdoutFile.deleteSync();
  }
}

void main(List<String> args) {
  setUp(_deleteTestData);
  tearDown(_deleteTestData);

  // Confirm that average power and duration are as expected when power readings
  // are (unrealistically) constant.
  test('constant power', () async {
    final stderrLines = <String>[
      '0,0.0015,14.0,2.0',
      '1000,0.0015,14.0,2.0',
      '2000,0.0015,14.0,2.0',
      '3000,0.0015,14.0,2.0',
      '4000,0.0015,14.0,2.0',
      '5000,0.0015,14.0,2.0',
      '6000,0.0015,14.0,2.0',
      '7000,0.0015,14.0,2.0',
      '8000,0.0015,14.0,2.0',
      '9000,0.0015,14.0,2.0',
      '10000,0.0015,14.0,2.0',
    ];
    await _setupTestData(stderrLines);

    final dump = MockDump();
    final performance = MockPerformance();
    final power = sl4f.Power(_fakeZedmonPath, dump, performance);
    await power.startRecording();
    await _waitForZedmon(power, 10000);

    final mockFile = MockFile();
    when(dump.writeAsString('power', 'fuchsiaperf.json', any))
        .thenAnswer((_) => Future.value(mockFile));
    await power.stopRecording(_converterPath);

    // Make sure the average power written to power.fuchsiaperf.json is correct.
    final encoded =
        verify(dump.writeAsString('power', 'fuchsiaperf.json', captureAny))
            .captured
            .single;
    final data = json.decode(encoded);
    expect(data.length, 1);
    expect(data[0]['values'].length, 1);
    expect(data[0]['values'][0], closeTo(2.0, 1e-8));

    verify(performance.convertResults(
        _converterPath, argThat(same(mockFile)), any));
  });

  // Confirm that average power and duration are as expected when power readings
  // vary.
  test('varying power', () async {
    expect(File(_stderrPath).existsSync(), false);
    final stderrLines = <String>[
      '0,0.0015,14.0,2.0',
      '1000,0.0015,14.0,2.1',
      '2000,0.0015,14.0,2.2',
      '3000,0.0015,14.0,2.3',
      '4000,0.0015,14.0,2.4',
      '5000,0.0015,14.0,2.5',
      '6000,0.0015,14.0,2.6',
      '7000,0.0015,14.0,2.7',
      '8000,0.0015,14.0,2.8',
      '9000,0.0015,14.0,2.9',
    ];
    await _setupTestData(stderrLines);

    final dump = MockDump();
    final performance = MockPerformance();
    final power = sl4f.Power(_fakeZedmonPath, dump, performance);
    await power.startRecording();
    await _waitForZedmon(power, 9000);

    final mockFile = MockFile();
    when(dump.writeAsString('power', 'fuchsiaperf.json', any))
        .thenAnswer((_) => Future.value(mockFile));
    await power.stopRecording(_converterPath);

    // Make sure the average power written to power.fuchsiaperf.json is correct.
    final encoded =
        verify(dump.writeAsString('power', 'fuchsiaperf.json', captureAny))
            .captured
            .single;
    final data = json.decode(encoded);
    expect(data.length, 1);
    expect(data[0]['values'].length, 1);
    expect(data[0]['values'][0], closeTo(2.4, 1e-8));

    verify(performance.convertResults(
        _converterPath, argThat(same(mockFile)), any));
  });
}
