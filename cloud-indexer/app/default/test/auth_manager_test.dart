// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:cloud_indexer/auth_manager.dart';
import 'package:googleapis/admin/directory_v1.dart' as directory_api;
import 'package:googleapis/oauth2/v2.dart' as oauth2_api;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

class MockOAuth2Api extends Mock implements oauth2_api.Oauth2Api {}

class MockAdminApi extends Mock implements directory_api.AdminApi {}

class MockMembersResourceApi extends Mock
    implements directory_api.MembersResourceApi {}

main() {
  group('authenticatedUser', () {
    const String reasonableToken = 'reasonable-token';
    const String reasonableAuthorization = 'Bearer $reasonableToken';

    final String domainValidEmail =
        'john@${AuthManager.authorizedDomains.first}';
    final String testEmail = 'foo@bar.baz';

    oauth2_api.Oauth2Api oauth2api;
    directory_api.AdminApi adminApi;
    directory_api.MembersResourceApi membersResourceApi;

    setUp(() {
      oauth2api = new MockOAuth2Api();
      adminApi = new MockAdminApi();
      membersResourceApi = new MockMembersResourceApi();
      when(adminApi.members).thenReturn(membersResourceApi);
    });

    tearDown(() {
      oauth2api = null;
      adminApi = null;
      membersResourceApi = null;
    });

    test('Malformed token.', () async {
      final String malformedAuthorization = 'Bear questionable-token';
      final AuthManager authManager = new AuthManager(oauth2api, adminApi);
      expect(
          await authManager.checkAuthenticated(malformedAuthorization), false);
    });

    test('Missing email scope.', () async {
      when(oauth2api.tokeninfo(accessToken: reasonableToken))
          .thenReturn(new Future.value(new oauth2_api.Tokeninfo()));
      final AuthManager authManager = new AuthManager(oauth2api, adminApi);
      expect(
          await authManager.checkAuthenticated(reasonableAuthorization), false);
    });

    test('Email outside of accepted domains and groups.', () async {
      when(oauth2api.tokeninfo(accessToken: reasonableToken)).thenReturn(
          new Future.value(new oauth2_api.Tokeninfo()..email = testEmail));
      when(membersResourceApi.get(AuthManager.authorizedGroup, testEmail))
          .thenAnswer((i) => throw new directory_api.DetailedApiRequestError(
              404, 'Member not found.'));
      final AuthManager authManager = new AuthManager(oauth2api, adminApi);
      expect(
          await authManager.checkAuthenticated(reasonableAuthorization), false);
    });

    test('Reasonable, group-authenticated request.', () async {
      when(oauth2api.tokeninfo(accessToken: reasonableToken)).thenReturn(
          new Future.value(new oauth2_api.Tokeninfo()..email = testEmail));
      when(membersResourceApi.get(AuthManager.authorizedGroup, testEmail))
          .thenReturn(
              new Future.value(new directory_api.Member()..email = testEmail));
      final AuthManager authManager = new AuthManager(oauth2api, adminApi);
      expect(
          await authManager.checkAuthenticated(reasonableAuthorization), true);
    });

    test('Reasonable, domain-authenticated request.', () async {
      when(oauth2api.tokeninfo(accessToken: reasonableToken)).thenReturn(
          new Future.value(
              new oauth2_api.Tokeninfo()..email = domainValidEmail));
      final AuthManager authManager = new AuthManager(oauth2api, adminApi);
      expect(
          await authManager.checkAuthenticated(reasonableAuthorization), true);
    });
  });
}
