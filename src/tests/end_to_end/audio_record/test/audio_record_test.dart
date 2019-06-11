// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);
const extension = 'wav';

double _averageSignalData(List<int> audioBytes) {
  int sum = 0;
  for (int i = 44; i < audioBytes.length; i++) {
    sum += audioBytes[i];
  }
  return sum / (audioBytes.length - 44);
}

double _varianceSignalData(List<int> audioBytes, double average) {
  double variance = 0;
  for (int i = 44; i < audioBytes.length; i++) {
    variance += pow((audioBytes[i] - average), 2);
  }
  return variance / (audioBytes.length - 44);
}

void _validateFile(File file) {
  if (!file.existsSync()) {
    fail('${file.path} does not exist');
  }
  if (file.lengthSync() != 288644) {
    fail('${file.path} is not of right size');
  }
  final audioBytes = file.readAsBytesSync();
  double average = _averageSignalData(audioBytes);
  if (average < 0.1) {
    fail('${file.path} has only silence and no real data');
  }
  double variance = _varianceSignalData(audioBytes, average);
  if (variance < 0.1) {
    fail('${file.path} data has low variance and is not normal');
  }
}

String _fileNameToTargetPath(String fileName) {
  return '/tmp/$fileName.$extension';
}

Future<File> _downloadAudioFile(
    sl4f.Sl4f sl4f, sl4f.Dump dump, String fileName) async {
  final filePath = _fileNameToTargetPath(fileName);
  final String response =
      await sl4f.request('traceutil_facade.GetTraceFile', {'path': filePath});
  return dump.writeAsBytes('$fileName', extension, base64.decode(response));
}

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Dump dumpDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    dumpDriver = sl4f.Dump();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('audio record creates valid recording', () async {
    // The test will run twice to make sure that the driver is inited properly
    // for recording. And stopping recording does proper teardown so that the
    // driver is capable of starting recording the second time.
    String fileNameOne = 't1';
    final proc1 = await sl4fDriver.sshProcess(
        'audio -r 48000 -c 2 -b 16 record ${_fileNameToTargetPath(fileNameOne)} 2');
    await proc1.stdout.transform(utf8.decoder).join();
    File fileOne =
        await _downloadAudioFile(sl4fDriver, dumpDriver, fileNameOne);
    _validateFile(fileOne);

    String fileNameTwo = 't2';
    final proc2 = await sl4fDriver.sshProcess(
        'audio -r 48000 -c 2 -b 16 record ${_fileNameToTargetPath(fileNameTwo)} 2');
    await proc2.stdout.transform(utf8.decoder).join();
    File fileTwo =
        await _downloadAudioFile(sl4fDriver, dumpDriver, fileNameTwo);
    _validateFile(fileTwo);
  },
      // This is a large test that waits for the DUT to come up.
      timeout: Timeout(_timeout));
}
