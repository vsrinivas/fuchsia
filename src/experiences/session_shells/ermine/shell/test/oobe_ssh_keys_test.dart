// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ssh/fidl_async.dart';
import 'package:http/http.dart' as http;
import 'package:internationalization/strings.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/widgets/oobe/ssh_keys.dart';

const String kTestUsername = 'test_username';

void main() {
  MockKeysProxy control;

  setUp(() async {
    control = MockKeysProxy();
  });

  test('SshKeysModel returns list of keys if http call is successful.',
      () async {
    final client = MockClient();

    final model = SshKeysModel(control: control, client: client)
      ..username = kTestUsername;

    when(client.get(Uri.https(kGithubApiUrl, '/users/$kTestUsername/keys')))
        .thenAnswer((_) async => http.Response(
            '[{"id": 1, "key": "test_key1"}, {"id": 2, "key": "test_key2"}]',
            200));

    expect(await model.fetchKey(), ['test_key1', 'test_key2']);
  });

  test('SshKeysModel returns empty list if http call returns 404 error.',
      () async {
    final client = MockClient();
    final model = SshKeysModel(control: control, client: client)
      ..username = kTestUsername;

    when(client.get(Uri.https(kGithubApiUrl, '/users/$kTestUsername/keys')))
        .thenAnswer((_) async => http.Response('Not Found', 404));

    expect(await model.fetchKey(), []);
  });

  test('SshKeysModel navigate github workflow.', () async {
    final client = MockClient();
    final model = SshKeysModel(control: control, client: client);

    expect(model.visibleScreen.value, Screen.add);
    expect(model.importMethod.value, ImportMethod.github);

    model.controller.text = kTestUsername;
    when(client.get(Uri.https('api.github.com', '/users/$kTestUsername/keys')))
        .thenAnswer((_) async => http.Response('Not Found', 404));

    await model.onAdd();

    expect(model.visibleScreen.value, Screen.error);

    model.showAdd();

    expect(model.visibleScreen.value, Screen.add);

    when(client.get(Uri.https('api.github.com', '/users/$kTestUsername/keys')))
        .thenAnswer(
            (_) async => http.Response('[{"id": 1, "key": "test_key"}]', 200));

    await model.onAdd();

    expect(model.visibleScreen.value, Screen.confirm);

    when(control.addKey(SshAuthorizedKeyEntry(key: 'test_key')))
        .thenAnswer((_) => Future.value('test_key'));

    await model.confirmKey();

    verify(control.addKey(SshAuthorizedKeyEntry(key: 'test_key'))).called(1);
    expect(model.visibleScreen.value, Screen.exit);
  });

  test('SshKeysModel navigate manual workflow.', () async {
    final model = SshKeysModel(control: control);

    expect(model.visibleScreen.value, Screen.add);

    model.importMethod.value = ImportMethod.manual;
    model.controller.text = 'test_key';

    when(control.addKey(SshAuthorizedKeyEntry(key: 'test_key')))
        .thenAnswer((_) => Future.value('test_key'));

    await model.onAdd();

    verify(control.addKey(SshAuthorizedKeyEntry(key: 'test_key'))).called(1);
    expect(model.visibleScreen.value, Screen.exit);
  });

  test('SshKeysModel shows error messages.', () async {
    final client = MockClient();
    final model = SshKeysModel(control: control, client: client);

    expect(model.visibleScreen.value, Screen.add);
    expect(model.importMethod.value, ImportMethod.github);

    model.controller.text = kTestUsername;
    when(client.get(Uri.https('api.github.com', '/users/$kTestUsername/keys')))
        .thenAnswer(
            (_) async => http.Response('', 400, reasonPhrase: 'Bad Request'));

    await model.onAdd();

    expect(model.visibleScreen.value, Screen.error);
    expect(model.errorMessage,
        Strings.oobeSshKeysHttpErrorDesc(400, 'Bad Request'));

    model.showAdd();

    when(client.get(Uri.https('api.github.com', '/users/$kTestUsername/keys')))
        .thenAnswer(
            (_) async => http.Response('', 404, reasonPhrase: 'Not Found'));

    await model.onAdd();

    expect(model.visibleScreen.value, Screen.error);
    expect(
        model.errorMessage, Strings.oobeSshKeysGithubErrorDesc(kTestUsername));

    when(client.get(Uri.https('api.github.com', '/users/$kTestUsername/keys')))
        .thenAnswer(
            (_) async => http.Response('[{"id": 1, "key": "test_key"}]', 200));

    await model.onAdd();

    expect(model.visibleScreen.value, Screen.confirm);

    when(control.addKey(SshAuthorizedKeyEntry(key: 'test_key')))
        .thenAnswer((_) => Future.error('test error status'));

    await model.confirmKey();

    verify(control.addKey(SshAuthorizedKeyEntry(key: 'test_key'))).called(1);
    expect(model.visibleScreen.value, Screen.error);
    expect(model.errorMessage, Strings.oobeSshKeysFidlErrorDesc);
  });
}

// Mock classes.
class MockClient extends Mock implements http.Client {}

class MockKeysProxy extends Mock implements AuthorizedKeysProxy {}
