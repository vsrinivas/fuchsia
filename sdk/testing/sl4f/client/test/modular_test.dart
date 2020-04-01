// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

class MockSsh extends Mock implements Ssh {}

void main(List<String> args) {
  HttpServer fakeServer;
  Sl4f sl4f;
  MockSsh ssh;

  setUp(() async {
    ssh = MockSsh();
    fakeServer = await HttpServer.bind('127.0.0.1', 18080);
    sl4f = Sl4f('127.0.0.1', ssh, 18080);
  });

  tearDown(() async {
    await fakeServer.close();
  });

  test('call RestartSession facade with no params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.RestartSession');
      expect(body['params'], null);
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).restartSession(), completion(equals('Success')));
  });

  test('isRunning: no', () {
    when(ssh.run(any))
        .thenAnswer((_) => Future.value(ProcessResult(31337, 0, '', '')));
    expect(Modular(sl4f).isRunning, completion(equals(false)));
  });

  test('isRunning: yes', () {
    when(ssh.run(any)).thenAnswer(
        (_) => Future.value(ProcessResult(31337, 0, 'basemgr', '')));
    expect(Modular(sl4f).isRunning, completion(equals(true)));
  });

  test('isRunning: error', () {
    when(ssh.run(any)).thenAnswer(
        (_) => Future.value(ProcessResult(31337, -1, '', 'blaah!')));
    expect(Modular(sl4f).isRunning, completion(equals(false)));
  });

  test('call KillBasemgr facade with no params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.KillBasemgr');
      expect(body['params'], null);
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).killBasemgr(), completion(equals('Success')));
  });
}
