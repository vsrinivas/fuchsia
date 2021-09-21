// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:typed_data';

import 'package:googleapis/speech/v1.dart' as gcloud;
import 'package:logging/logging.dart';

final _log = Logger('audio_matchers');

/// Maximum request size for cloud speech RPCs is 10 MB.
///
/// We reserve 1/4 of the quota for base64 encoding.
///
/// Sending a palyload bigger than this results in an 400 error.
/// [audio limits](https://cloud.google.com/speech-to-text/quotas#content)
const _speechPayloadLimitBytes = 10 * 1024 * 1024 * 3 / 4;

/// Allow 1024 bytes of overhead in audio data payload.
const _bufferSizeBytes = 1024;

/// Safe guard maximum payload we send to Cloud Speech by [_bufferSizeBytes]
const _maxSTTRequestSizeBytes = _speechPayloadLimitBytes - _bufferSizeBytes;

/// googleapis/speech/v1.dart RecognitionConfig is missing multichannel support
class RecognitionConfig extends gcloud.RecognitionConfig {
  int audioChannelCount;
  bool enableSeparateRecognitionPerChannel;

  @override
  Map<String, dynamic> toJson() {
    final json = super.toJson();
    if (audioChannelCount != null) {
      json['audioChannelCount'] = audioChannelCount;
    }

    if (enableSeparateRecognitionPerChannel != null) {
      json['enableSeparateRecognitionPerChannel'] =
          enableSeparateRecognitionPerChannel;
    }
    return json;
  }
}

/// Invokes the speech to text API on a byte array [data] containing an audio
/// file.
///
/// Returns a list of transcripts, potentially (but not necessarily) for
/// each audio channel.
Future<Iterable<String>> speechToText(
    gcloud.SpeechApi api, List<int> data, String languageCode) async {
  // Field selector constructed using
  // https://developers.google.com/apis-explorer fields editor.
  const $fields = 'results/alternatives/transcript';
  List<int> audioData = data;

  if (!data.every((element) => (element >= 0 && element <= 255))) {
    _log.warning('Found invalid audio data, data needs to be 8-bit value.');

    // Force data to be 8-bit.
    audioData = Uint8List.fromList(data);
  }

  if (audioData.length > _maxSTTRequestSizeBytes) {
    _log.warning('Truncating speechToText audio to ${_maxSTTRequestSizeBytes}B.'
        ' Data was ${data.length}B');
    audioData = data.sublist(0, _maxSTTRequestSizeBytes.toInt());
  }

  final request = gcloud.RecognizeRequest()
    ..config = (RecognitionConfig()
      ..languageCode = languageCode
      ..audioChannelCount = 2
      // Setting this to false only transcribes the first channel.
      ..enableSeparateRecognitionPerChannel = false)
    ..audio = (gcloud.RecognitionAudio()..contentAsBytes = audioData);
  _log.fine('Calling api.speech.recognize for ${$fields}');
  final results =
      (await api.speech.recognize(request, $fields: $fields)).results;
  if (results == null) {
    return null;
  }

  // We don't always get more than one result, but when we do, hopefully it's
  // because we did per-channel recognition.
  final transcripts = results.map((r) => r.alternatives.single.transcript);
  _log.info('Transcripts: ${transcripts.map((t) => '"$t"').join(', ')}');
  return transcripts;
}

/// Prints a warning if the transcription doesn't match [pat] or is empty.
void warnOnTranscriptionNotMatching(
    Iterable<String> transcriptions, Pattern pat) {
  if (transcriptions == null || transcriptions.isEmpty) {
    _log.warning('No recognized speech response');
    return;
  }
  final notMatched =
      transcriptions.firstWhere((t) => !t.contains(pat), orElse: () => null);
  if (notMatched != null) {
    _log.warning('Audio transcription "$notMatched" did not match "$pat"');
  }
}
