// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
import 'dart:convert';

import 'package:gcloud/storage.dart';
import 'package:googleapis/speech/v1.dart' as gcloud;
import 'package:googleapis/vision/v1.dart' as gcloud;
import 'package:googleapis_auth/auth_io.dart' as auth;
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

/// GCloud specific operations used in tests.
class GCloud {
  http.Client _cloudClient;
  http.Client get cloudClient => _cloudClient;

  gcloud.SpeechApi _speech;
  gcloud.SpeechApi get speech => _speech ?? gcloud.SpeechApi(_cloudClient);

  gcloud.VisionApi _vision;
  gcloud.VisionApi get vision => _vision ?? gcloud.VisionApi(_cloudClient);

  final _log = Logger('gcloud');

  GCloud();

  /// Creates an HTTP client properly authenticated for gcloud requests.
  ///
  /// The authentication mechanism may differ between developer desktops and
  /// infra hosts.
  GCloud.withClientViaApiKey(String apiKey)
      : _cloudClient = auth.clientViaApiKey(apiKey);

  /// Reads a file from GCS.
  ///
  /// Might throw a [gcloud.DetailedApiRequestError] if there's an error
  /// accesing the file.
  Future<String> readFromStorage(String bucketName, String filePathAndName,
      {String gcpProject = ''}) async {
    await setClientFromMetadata();
    final storage = Storage(_cloudClient, gcpProject);
    _log.info('Read filename $filePathAndName');
    return storage
        .bucket(bucketName)
        .read(filePathAndName)
        .transform(utf8.decoder)
        .join('');
  }

  /// Either takes the default _cloudClient created from factory constructor
  /// [GCloud.withClientViaApiKey], or create a new client using
  /// [auth.clientViaMetadataServer].
  Future<void> setClientFromMetadata() async =>
      _cloudClient ??= await auth.clientViaMetadataServer();

  void close() => _cloudClient?.close();
}
