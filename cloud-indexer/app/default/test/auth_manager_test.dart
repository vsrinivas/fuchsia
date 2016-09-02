// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:cloud_indexer/auth_manager.dart';
import 'package:googleapis/oauth2/v2.dart' as oauth2_api;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

class MockOAuth2Api extends Mock implements oauth2_api.Oauth2Api {}

main() {
  group('authenticatedUser', () {
    const String reasonableToken = 'reasonable-token';
    const String reasonableAuthorization = 'Bearer $reasonableToken';

    test('Malformed token.', () {
      final String malformedAuthorization = 'Bear questionable-token';
      oauth2_api.Oauth2Api api = new MockOAuth2Api();
      new AuthManager(api)
          .authenticatedUser(malformedAuthorization)
          .then((String email) => expect(email, null));
    });

    test('Missing email scope.', () {
      oauth2_api.Oauth2Api api = new MockOAuth2Api();
      when(api.tokeninfo(accessToken: reasonableToken))
          .thenReturn(new Future.value(new oauth2_api.Tokeninfo()));
      new AuthManager(api)
          .authenticatedUser(reasonableAuthorization)
          .then((String email) => expect(email, null));
    });

    test('Email outside of accepted domains.', () {
      oauth2_api.Oauth2Api api = new MockOAuth2Api();
      when(api.tokeninfo(accessToken: reasonableToken)).thenReturn(
          new Future.value(new oauth2_api.Tokeninfo()..email = 'foo@bar.com'));
      new AuthManager(api)
          .authenticatedUser(reasonableAuthorization)
          .then((String email) => expect(email, null));
    });

    test('Reasonable, authenticated request.', () {
      final String testEmail = 'foo@google.com';
      oauth2_api.Oauth2Api api = new MockOAuth2Api();
      when(api.tokeninfo(accessToken: reasonableToken)).thenReturn(
          new Future.value(new oauth2_api.Tokeninfo()..email = testEmail));
      new AuthManager(api)
          .authenticatedUser(reasonableAuthorization)
          .then((String email) => expect(email, testEmail));
    });
  });
}
