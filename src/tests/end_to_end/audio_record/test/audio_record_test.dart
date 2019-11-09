// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:math';
import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);
const extension = 'wav';
const int kWavHeaderLength = 44;
const int kBitsPerSample = 16;
int kBytesPerSample = (kBitsPerSample ~/ 8);

// Create an int16 value from two incoming uint8 values
int _int16FromBytes(int lsbyte, int msbyte) {
  int result = lsbyte + (msbyte << 8);
  // After shifting to create an uint16, we must convert to int16
  if (result >= 0x8000) {
    result -= 0x10000;
  }
  return result;
}

bool _isDataAllZeros(List<int> audioBytes) {
  for (int i = kWavHeaderLength; i < audioBytes.length; i += kBytesPerSample) {
    int a = _int16FromBytes(audioBytes[i], audioBytes[i + 1]);
    if (a != 0) {
      return false;
    }
  }
  return true;
}

double _averageSignalData(List<int> audioBytes, int numSamples) {
  double sum = 0.0;
  for (int i = kWavHeaderLength; i < audioBytes.length; i += kBytesPerSample) {
    int a = _int16FromBytes(audioBytes[i], audioBytes[i + 1]);
    sum += a;
  }
  return sum / numSamples;
}

double _varianceSignalData(
    List<int> audioBytes, double average, int numSamples) {
  double variance = 0.0;
  for (int i = kWavHeaderLength; i < audioBytes.length; i += kBytesPerSample) {
    int a = _int16FromBytes(audioBytes[i], audioBytes[i + 1]);
    variance += pow((a - average), 2);
  }
  return variance / numSamples;
}

void _validateFile(File file) {
  if (!file.existsSync()) {
    fail('${file.path} does not exist');
  }
  if (file.lengthSync() != 288644) {
    fail('${file.path} is not of right size');
  }
  int numSamples = ((288644 - kWavHeaderLength) ~/ kBytesPerSample);
  final audioBytes = file.readAsBytesSync();
  if (_isDataAllZeros(audioBytes)) {
    fail('${file.path} has only silence and no real data');
  }
  double average = _averageSignalData(audioBytes, numSamples);
  double variance = _varianceSignalData(audioBytes, average, numSamples);
  if (variance < 0.001) {
    fail('${file.path} data has low variance and is not normal');
  }
}

String _fileNameToTargetPath(String fileName) {
  return '/tmp/$fileName.$extension';
}

Future<File> _downloadAudioFile(
    sl4f.Sl4f sl4fDriver, sl4f.Dump dump, String fileName) async {
  final filePath = _fileNameToTargetPath(fileName);
  final contents = await sl4f.Storage(sl4fDriver).readFile(filePath);
  return dump.writeAsBytes('$fileName', extension, contents);
}

void main() {
  sl4f.Sl4f sl4fDriver;
  Directory dumpDir;
  sl4f.Dump dumpDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    dumpDir = Directory.systemTemp.createTempSync('dump-test');
    dumpDriver = sl4f.Dump(dumpDir.path);
  });

  tearDown(() async {
    dumpDir.deleteSync(recursive: true);
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('audio record creates valid recording', () async {
    // The test will run twice to make sure that the driver is inited properly
    // for recording. And stopping recording does proper teardown so that the
    // driver is capable of starting recording the second time.

    // audio commands require direct access to driver which will not work if
    // audio_core is holding on to the resource. Hence explicitly killing it
    // before trying the audio record command.
    await sl4fDriver.ssh.run('killall audio_core.cmx');
    String fileNameOne = 'test_1';
    // 'audio' utility will capture 48kHz 2-chan 16-bit audio directly from
    // the audio driver for 2 secs and save it to a WAV file.
    final proc1 = await sl4fDriver.ssh.run(
        'audio -r 48000 -c 2 -b $kBitsPerSample record ${_fileNameToTargetPath(fileNameOne)} 2');
    expect(proc1.exitCode, equals(235)); // ZX_ERR_TIMED_OUT

    File fileOne =
        await _downloadAudioFile(sl4fDriver, dumpDriver, fileNameOne);
    _validateFile(fileOne);

    String fileNameTwo = 'test_2';
    final proc2 = await sl4fDriver.ssh.run(
        'audio -r 48000 -c 2 -b $kBitsPerSample record ${_fileNameToTargetPath(fileNameTwo)} 2');
    expect(proc2.exitCode, equals(235)); // ZX_ERR_TIMED_OUT
    File fileTwo =
        await _downloadAudioFile(sl4fDriver, dumpDriver, fileNameTwo);
    _validateFile(fileTwo);
  },
      // This is a large test that waits for the DUT to come up.
      timeout: Timeout(_timeout));
}
