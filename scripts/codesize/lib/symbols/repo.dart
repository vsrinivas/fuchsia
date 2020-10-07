// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

library symbols_repo;

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:googleapis_auth/auth_io.dart' as auth;
import 'package:http/http.dart' as http;
import 'package:gcloud/storage.dart';

import '../build.dart';
import '../common_util.dart';
import '../crash_handling.dart' show KnownFailure;
import 'cache.dart';

// ignore: avoid_classes_with_only_static_members
/// Provides a singleton `http.Client` instance that is authorized with
/// Google Cloud.
class GoogleApiClient {
  /// Obtains a cached client. The client should be closed by
  /// calling `GoogleApiClient.close` on exit.
  static Future<http.Client> getDefault() {
    if (_client != null) {
      return _client;
    }
    return _client = _getDefault();
  }

  static Future<void> close() async {
    if (_client != null) {
      final client = await _client;
      client.close();
      _client = null;
    }
    if (_baseClient != null) {
      _baseClient.close();
      _baseClient = null;
    }
  }

  static Future<http.Client> _getDefault() async {
    final homeDir = Directory(Platform.environment['HOME']);
    final File credentialFile =
        homeDir / File('.config/gcloud/application_default_credentials.json');
    if (!credentialFile.existsSync()) {
      throw KnownFailure('''
Cannot find Google Cloud Application Default Credentials.

Please install the Google Cloud SDK via:

    sudo apt install -y google-cloud-sdk

and run the following command to obtain the credentials:

    gcloud auth application-default login

See https://cloud.google.com/sdk/gcloud/reference/auth/application-default/login
''');
    }
    final jsonCredentials = jsonDecode(await credentialFile.readAsString());
    final scopes = <String>[]..addAll(Storage.SCOPES);
    final baseClient = http.Client();
    // By default, running `gcloud auth application-default login` yields
    // a credential of the "authorized_user" type. We need to exchange that
    // for a crediential that is supported by the dart gcloud library.
    final userCred = auth.AccessCredentials(
        auth.AccessToken(jsonCredentials['type'], '', DateTime.now().toUtc()),
        jsonCredentials['refresh_token'],
        scopes);
    final appCred = await auth.refreshCredentials(
        auth.ClientId(
          jsonCredentials['client_id'],
          jsonCredentials['client_secret'],
        ),
        userCred,
        baseClient);
    // Stow away baseClient to close later. Closing the authenticated
    // client would not close the base client.
    _baseClient = baseClient;
    return auth.authenticatedClient(baseClient, appCred);
  }

  static Future<http.Client> _client;
  static http.Client _baseClient;
}

class CloudRepo {
  CloudRepo(this.client, this.bucket, this.namespace, this.cache);

  static Future<CloudRepo> create(String gcsUrl, Cache cache) async {
    final storage = Storage(await GoogleApiClient.getDefault(), null);
    final uri = Uri.parse(gcsUrl);
    final host = uri.host;
    final namespace = removePrefix(uri.path, '/');
    final bucket = storage.bucket(host);
    return CloudRepo(storage, bucket, namespace, cache);
  }

  Future<String> getBuildObject(String buildId) async {
    final maybeEntry = await cache.getEntry(buildId);
    if (maybeEntry != null) {
      return maybeEntry;
    }
    final stream = bucket.read('$namespace/$buildId.debug');
    return cache.addEntry(buildId, stream);
  }

  final Storage client;
  final Bucket bucket;
  final String namespace;
  final Cache cache;
}
