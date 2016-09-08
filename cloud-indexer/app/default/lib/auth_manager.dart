// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:cloud_indexer_common/wrappers.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:googleapis/admin/directory_v1.dart' as directory_api;
import 'package:googleapis/oauth2/v2.dart' as oauth2_api;
import 'package:googleapis_auth/auth_io.dart' as auth_io;
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

final Logger _logger = new Logger('cloud_indexer.auth_service');

const Symbol _authManagerServiceKey = #authManagerService;

AuthManager get authManagerService => ss.lookup(_authManagerServiceKey);

void registerAuthManagerService(AuthManager authManager) {
  ss.register(_authManagerServiceKey, authManager);
}

class AuthManager {
  static const List<String> authorizedDomains = const ['google.com'];
  static const String authorizedGroup = 'mojodeveloppers@mojoapps.io';

  // TODO(victorkwan): Move these out into configuration.
  static const String _keyPath = 'auth/key.json';
  static const String _mojoAdmin = 'admin@mojoapps.io';

  final RegExp _authRegExp = new RegExp(r'^Bearer\s+([^\s]+)$');
  final oauth2_api.Oauth2Api _oauth2api;
  final directory_api.AdminApi _adminApi;

  bool _isAuthorizedDomain(String email) =>
      authorizedDomains.any((String domain) => email.endsWith(domain));

  /// Creates an [AuthManager] given an authenticated [client].
  ///
  /// Although the client we obtain from the metadata server allows us to
  /// perform OAuth2 queries, we need to retrieve our service account
  /// credentials to produce impersonated credentials. Hence, we retrieve
  /// these from Cloud Storage.
  static Future<AuthManager> fromClient(
      http.Client client, String bucketName) async {
    final StorageBucketWrapper storageBucketWrapper =
        new StorageBucketWrapper(client, bucketName);
    auth_io.ServiceAccountCredentials mojoCredentials =
        new auth_io.ServiceAccountCredentials.fromJson(
            await storageBucketWrapper
                .readObject(_keyPath)
                .transform(UTF8.decoder)
                .transform(JSON.decoder)
                .single,
            impersonatedUser: _mojoAdmin);
    auth_io.AuthClient mojoClient = await auth_io.clientViaServiceAccount(
        mojoCredentials,
        [directory_api.AdminApi.AdminDirectoryGroupMemberReadonlyScope]);

    return new AuthManager(new oauth2_api.Oauth2Api(client),
        new directory_api.AdminApi(mojoClient));
  }

  AuthManager(this._oauth2api, this._adminApi);

  /// Returns the email of the authenticated user given an [accessToken].
  ///
  /// Currently, users outside of the google.com domain are considered
  /// unauthenticated. In this case, we return null.
  Future<String> authenticatedUser(String accessToken) async {
    if (accessToken == null) {
      _logger.info('No access token provided. Bailing out.');
      return null;
    }

    Match match = _authRegExp.matchAsPrefix(accessToken);
    if (match == null) {
      _logger.info('Invalid access token. Bailing out.');
      return null;
    }

    String email;
    try {
      oauth2_api.Tokeninfo tokeninfo =
          await _oauth2api.tokeninfo(accessToken: match.group(1));
      if (tokeninfo.email == null) {
        _logger.info('Authentication failed. The email scope was not present '
            'in the request.');
        return null;
      }
      email = tokeninfo.email;
      if (_isAuthorizedDomain(email)) return email;
    } on oauth2_api.DetailedApiRequestError catch (e) {
      _logger.warning('Authentication failed. Could not retrieve token info '
          '(${e.status}): ${e.message}');
      return null;
    }

    try {
      await _adminApi.members.get(authorizedGroup, email);
      return email;
    } on directory_api.DetailedApiRequestError catch (e) {
      if (e.status == HttpStatus.NOT_FOUND) {
        _logger.info('Authentication failed. The user was in neither the '
            'authorized domain nor group.');
      } else {
        _logger.warning('Authentication failed. Could not determine group '
            'membership (${e.status}): ${e.message}');
      }
      return null;
    }
  }
}
