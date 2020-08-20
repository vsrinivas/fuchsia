// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;
import 'dart:typed_data' show Uint8List;

import 'package:pedantic/pedantic.dart';

import 'dump.dart';
import 'sl4f_client.dart';

/// Injects and captures audio using the Virtual Audio Device interface with
/// SL4F's audio facade.
///
/// The audio injection behaves as a virtual microphone, and the audio capture
/// as a virtual speaker. Right now the audio files need to be uploaded to the
/// device first using [Audio.putInputAudio] before they can be injected using
/// [Audio.startInputInjection].
class Audio {
  final Sl4f _sl4f;
  final Dump _dump;

  /// Construct an [Audio] facade abstraction.
  Audio(this._sl4f, [Dump dump]) : _dump = dump ?? Dump();

  /// Uploads an audio [file] to be used as input number [index].
  ///
  /// Right now the audio file must be a 2-channel, signed 16bit, 16kHz WAVE
  /// file. See the VirtualAudio constructor in SL4F's audio facade.
  Future<void> putInputAudio(int index, File file) async {
    final audioBytes = await file.readAsBytes();
    await _sl4f.request('audio_facade.PutInputAudio', {
      'index': index,
      'data': base64Encode(audioBytes),
    });
  }

  /// Start injection of audio file stored at [index].
  Future<void> startInputInjection(int index) =>
      _sl4f.request('audio_facade.StartInputInjection', {'index': index});

  /// Starts capturing the audio output.
  Future<void> startOutputSave() =>
      _sl4f.request('audio_facade.StartOutputSave');

  /// Stops capturing the audio output.
  ///
  /// Use [getOutputAudio] to get the latest audio capture.
  Future<void> stopOutputSave() => _sl4f.request('audio_facade.StopOutputSave');

  /// Retrieves the latest audio capture.
  ///
  /// If [dumpName] is provided, the audio file will also be dumped with
  /// [dumpName] in the filename.
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

/// Simple container for Audio data from [Audio.getOutputAudio].
class AudioTrack {
  /// This is true if the entire audio capture was zeroes.
  ///
  /// This is a very naive way of checking for silence, it might be improved in
  /// the future if there's need for it.
  bool isSilence = false;

  /// A WAV file (including header).
  Uint8List audioData;
}
