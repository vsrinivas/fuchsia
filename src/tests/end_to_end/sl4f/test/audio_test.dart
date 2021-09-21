// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';
import 'dart:typed_data';

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Timeout(Duration(minutes: 1));

class MockFile extends Mock implements File {}

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Audio audio;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    audio = sl4f.Audio(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Audio, () {
    test('injects audio', () async {
      final file = MockFile();
      when(file.readAsBytes()).thenAnswer((_) async => Uint8List.fromList([
            // Just for fun, here's what these mean:
            0x52, 0x49, 0x46, 0x46, // "RIFF"
            0x28, 0x00, 0x00, 0x00, // 40 bytes total
            0x57, 0x41, 0x56, 0x45, // "WAVE"
            0x66, 0x6D, 0x74, 0x20, // "fmt "
            0x10, 0x00, 0x00, 0x00, // header size
            0x01, 0x00, // PCM encoding
            0x02, 0x00, // channels
            0x80, 0xBB, 0x00, 0x00, // sample rate
            0x00, 0xEE, 0x02, 0x00, // byte rate
            0x04, 0x00, // block align
            0x10, 0x00, // bits per sample
            0x64, 0x61, 0x74, 0x61, // "DATA"
            0x04, 0x00, 0x00, 0x00, // data size
            0xFF, 0x7F, 0x00, 0x80, // a blip.
          ]));

      // If anything throws an exception then we've failed.
      await audio.putInputAudio(0, file);
      await audio.startInputInjection(0);

      // TODO(isma): Figure out why `audio record` cannot open the audio device
      // so we can use it to test injection.
    });

    test('saves audio playback', () async {
      // If anything throws an exception then we've failed.
      await audio.startOutputSave();
      await audio.stopOutputSave();
      expect(audio.getOutputAudio(), completion(isNotNull));
    });
  }, timeout: _timeout);
}
