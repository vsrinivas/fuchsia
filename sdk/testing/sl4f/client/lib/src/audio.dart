// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;
import 'dart:typed_data' show Uint8List;

import 'package:pedantic/pedantic.dart';

import 'dump.dart';
import 'sl4f_client.dart';

class Audio {
  final Sl4f _sl4f;
  final Dump _dump;

  Audio(this._sl4f, [Dump dump]) : _dump = dump ?? Dump();

  // TODO(perley): Drop this after migrating internal clients.
  Future<void> putPlayback(File file) => putInputAudio(0, file);

  Future<void> putInputAudio(int index, File file) async {
    final audioBytes = await file.readAsBytes();
    await _sl4f.request('audio_facade.PutInputAudio', {
      'index': index,
      'data': base64Encode(audioBytes),
    });
  }

  Future<void> startInputInjection(int index) =>
      _sl4f.request('audio_facade.StartInputInjection', {'index': index});

  Future<void> startOutputSave() =>
      _sl4f.request('audio_facade.StartOutputSave');

  Future<void> stopOutputSave() => _sl4f.request('audio_facade.StopOutputSave');

  Future<AudioTrack> getOutputAudio({String dumpName}) async {
    // The response body is just base64encoded audio
    final String response = await _sl4f.request('audio_facade.GetOutputAudio');
    final Uint8List bytes = base64Decode(response);
    bool silence = true;
    // There is a 44 byte wave header in the returned data, each sample is 2
    // bytes
    for (int i = 44; i < bytes.length; i += 2) {
      // We test left/right/left/right for each sample, so just do 1 int16 at a
      // time.
      final int value = ((bytes[i] << 8) | (bytes[i + 1]));
      silence = value == 0;
      if (!silence) {
        break;
      }
    }

    if (dumpName != null) {
      unawaited(_dump.writeAsBytes(dumpName, 'wav', bytes));
    }

    return AudioTrack()
      ..audioData = bytes
      ..isSilence = silence;
  }
}

class AudioTrack {
  // TODO: Implement a proper way to test this and a proper audio container.
  bool isSilence = false;
  // This is a wav file
  Uint8List audioData;
}
