// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

void main(List<String> args) {
  HttpServer fakeServer;
  Sl4f sl4f;

  setUp(() async {
    fakeServer = await HttpServer.bind('127.0.0.1', 18080);
    sl4f = Sl4f('127.0.0.1:18080', null);
  });

  tearDown(() async {
    await fakeServer.close();
  });

  test('call Launch with args', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'launch_facade.Launch');
      expect(body['params'], {
        'url': 'fake_url',
        'arguments': ['--arg1=arg1value', '--arg2=arg2value']
      });
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(
        Launch(sl4f)
            .launch('fake_url', ['--arg1=arg1value', '--arg2=arg2value']),
        completion(equals('Success')));
  });

  test('call Launch with no arg', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'launch_facade.Launch');
      expect(body['params'], {'url': 'fake_url'});
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Launch(sl4f).launch('fake_url'), completion(equals('Success')));
  });
}
