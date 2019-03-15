// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;
import 'dart:typed_data' show Uint8List;
import 'sl4f_client.dart';

class Audio {
  final Sl4f _sl4f;

  Audio([Sl4f sl4f]) : _sl4f = sl4f ?? Sl4f.fromEnvironment();

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  // TODO(perley): Drop this after migrating internal clients.
  Future<void> putPlayback(File file) async {
    return await putInputAudio(0, file);
  }

  Future<void> putInputAudio(int index, File file) async {
    final audioBytes = file.readAsBytesSync();
    await _sl4f.request('audio_facade.PutInputAudio', {
      'index': index,
      'data': base64Encode(audioBytes),
    });
  }

  Future<void> startInputInjection(int index) async {
    await _sl4f.request('audio_facade.StartInputInjection', {
      'index': index,
    });
  }

  // TODO(perley): Drop this after migrating internal clients.
  Future<void> startCapture() async {
    return await startOutputSave();
  }

  Future<void> startOutputSave() async {
    await _sl4f.request('audio_facade.StartOutputSave');
  }

  // TODO(perley): Drop this after migrating internal clients.
  Future<AudioTrack> getCapture() async {
    await stopOutputSave();
    return await getOutputAudio();
  }

  Future<void> stopOutputSave() async {
    await _sl4f.request('audio_facade.StopOutputSave');
  }

  Future<AudioTrack> getOutputAudio() async {
    // The response body is just base64encoded audio
    String response = await _sl4f.request('audio_facade.GetOutputAudio');
    return AudioTrack()
      ..audioData = base64Decode(response)
      ..isSilence = response.isEmpty;
  }
}

class AudioTrack {
  // TODO: Implement a proper way to test this and a proper audio container.
  bool isSilence = false;
  // This is a wav file
  Uint8List audioData;
}
