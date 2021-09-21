// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';

import 'package:googleapis/vision/v1.dart' as gcloud;
import 'package:image/image.dart';
import 'package:logging/logging.dart';
import 'package:matcher/matcher.dart';
import 'package:meta/meta.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

import 'gcloud_exceptions.dart';

final _log = Logger('image_matchers');

/// The number of times to retry a Cloud Vision call.
const _visionMaxTries = 2;

/// The amount of time to wait between attempts at talking to the vision server.
/// We should switch to exponential backoff if we want more than 2 attempts.
const _visionTryDelay = Duration(seconds: 2);

/// Error codes that can be retried:
///  DEADLINE_EXCEEDED (4) - Operation took longer than deadline.
///  ABORTED (10) - Operation was aborted due to transaction (or similar).
///  INTERNAL (13) - Server went kaput.
///  UNAVAILABLE (14) - Server went temporarily kaput.
/// We retry on INTERNAL because based on unscientific anecdata it seems that
/// whenever it happens for Cloud Vision the next request is still done
/// successfully.
/// We don't retry on RESOURCE_EXHAUSTED (8) as is unlikely we'll have quota
/// on the next retry.
/// Note that non-idempotent requests may not be retriable despite this.
bool _isRetriableError(int code) =>
    code == 4 || code == 10 || code == 13 || code == 14;

/// Calls Cloud Vision's vision.images.annotate API with retries.
///
/// Throws [VisionException] if there's a non-retriable error or if it fails
/// after retrying.
Future<gcloud.AnnotateImageResponse> invokeAnnotate(gcloud.VisionApi vision,
    {@required String $fields,
    @required gcloud.BatchAnnotateImagesRequest request,
    String dumpName,
    sl4f.Dump dump}) async {
  final output = dump ?? sl4f.Dump();
  final jsonEncoder = JsonUtf8Encoder('  ');
  gcloud.AnnotateImageResponse response;
  for (int attempt = 1; attempt <= _visionMaxTries; attempt++) {
    _log.fine('Calling vision.images.annotate for ${$fields}');
    final batchResponse =
        await vision.images.annotate(request, $fields: $fields);

    if (dumpName != null && dumpName.isNotEmpty) {
      // ignore: unawaited_futures
      output.writeAsBytes('$dumpName-image-annotate', 'json',
          jsonEncoder.convert(batchResponse.toJson()));
    }

    final response = batchResponse.responses.single;
    if (response.error == null) {
      return response;
    }

    if (!_isRetriableError(response.error.code)) {
      throw VisionException(response.error);
    }

    if (attempt < _visionMaxTries) {
      _log.warning('Retrying Cloud Vision request after $_visionTryDelay.');
      await Future.delayed(_visionTryDelay);
    }
  }
  throw VisionException(response.error);
}

/// Annotates an [image] with web entities and OCR.
///
/// Throws [VisionException] if there's a problem communicating with Cloud
/// Vision.
Future<gcloud.AnnotateImageResponse> annotateImage(
    gcloud.VisionApi vision, Image image,
    {sl4f.Dump dump}) async {
  // Field selector constructed using
  // https://developers.google.com/apis-explorer fields editor.
  const $fields = 'responses(error,textAnnotations/'
      'description,webDetection(webEntities(description,score)))';

  final request = gcloud.BatchAnnotateImagesRequest()
    ..requests = [
      gcloud.AnnotateImageRequest()
        ..image = (gcloud.Image()..content = base64Encode(encodePng(image)))
        ..features = [
          gcloud.Feature()
            ..type = 'WEB_DETECTION'
            ..maxResults = 3,
          gcloud.Feature()
            ..type = 'TEXT_DETECTION'
            ..maxResults = 1
        ]
    ];

  return invokeAnnotate(vision, $fields: $fields, request: request, dump: dump);
}

// Log JSON formatted AnnotateImageResponse
void dumpAnnotateImageResponse(gcloud.AnnotateImageResponse response) =>
    _log.info('AnnotateImageResponse: '
        '${JsonEncoder.withIndent('  ').convert(response)}');

/// Matches a [gcloud.AnnotateImageResponse] with annotated text ([String])
/// that matches the given [matcher].
Matcher textAnnotations(Matcher matcher) =>
    TypeMatcher<gcloud.AnnotateImageResponse>().having((response) {
      // Assumptions:
      // * the first text annotation seems to contain all the recognized text
      // * textAnnotations is either null or nonempty
      final text = response.textAnnotations?.first?.description;
      _log.info('Text: $text');
      return text;
    }, 'text', matcher);

/// Matches a [gcloud.AnnotateImageResponse] with a primary web entity
/// description that matches the given [matcher].
Matcher imageWebEntity(Matcher matcher) =>
    TypeMatcher<gcloud.AnnotateImageResponse>().having((response) {
      // Assumptions:
      // * the first entity has the highest confidence
      // * webEntities is either null or nonempty
      final entity = response.webDetection?.webEntities?.first;
      _log.info('Image of: '
          '${entity == null ? 'null' : '${entity.description} '
              '(score: ${entity.score})'}');
      return entity?.description;
    }, 'primary entity', matcher);
