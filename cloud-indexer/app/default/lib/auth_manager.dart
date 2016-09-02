// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:gcloud/service_scope.dart' as ss;
import 'package:googleapis/oauth2/v2.dart' as oauth2_api;
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

final Logger _logger = new Logger('cloud_indexer.auth_service');

const Symbol _authManagerServiceKey = #authManagerService;

AuthManager get authManagerService => ss.lookup(_authManagerServiceKey);

void registerAuthManagerService(AuthManager authManager) {
  ss.register(_authManagerServiceKey, authManager);
}

// TODO(victorkwan): Update with group-based authentication. To do so, we
// provide service accounts with Admin SDK access. More information here:
// https://developers.google.com/admin-sdk/directory/v1/guides/delegation
class AuthManager {
  static const List<String> _authorizedDomains = const ['google.com'];

  final RegExp _authRegExp = new RegExp(r'^Bearer\s+([^\s]+)$');
  final oauth2_api.Oauth2Api _oauth2api;

  bool _isAuthorizedDomain(String email) =>
      _authorizedDomains.any((String domain) => email.endsWith(domain));

  AuthManager.fromClient(http.Client client)
      : _oauth2api = new oauth2_api.Oauth2Api(client);

  AuthManager(this._oauth2api);

  /// Returns the email of the authenticated user given an [accessToken].
  ///
  /// Currently, users outside of the google.com domain are considered
  /// unauthenticated. In this case, we return null.
  Future<String> authenticatedUser(String accessToken) async {
    Match match = _authRegExp.matchAsPrefix(accessToken);
    if (match == null) {
      _logger.info('Invalid access token. Bailing out.');
      return null;
    }
    try {
      oauth2_api.Tokeninfo tokeninfo =
          await _oauth2api.tokeninfo(accessToken: match.group(1));
      if (tokeninfo.email == null) {
        _logger.info('Authentication failed. The email scope was not present '
            'in the request.');
        return null;
      }
      return _isAuthorizedDomain(tokeninfo.email) ? tokeninfo.email : null;
    } on oauth2_api.DetailedApiRequestError catch (e) {
      _logger.warning('Authentication failed. Could not retrieve token info '
          '(${e.status}): ${e.message}');
      return null;
    }
  }
}
