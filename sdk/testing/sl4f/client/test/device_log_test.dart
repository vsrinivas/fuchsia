// Copyright 2019 The Fuchsia Authors. All rights reserved.
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
    fakeServer = await HttpServer.bind('127.0.0.1', 18081);
    sl4f = Sl4f('127.0.0.1', null, 18081);
  });

  tearDown(() async {
    await fakeServer.close();
    sl4f.close();
  });

  group(DeviceLog, () {
    test('error', () {
      void handler(HttpRequest req) async {
        expect(req.contentLength, greaterThan(0));
        final body = jsonDecode(await utf8.decodeStream(req));
        expect(body['method'], 'logging_facade.LogErr');
        expect(body['params'].keys.single, 'message');
        expect(body['params']['message'], 'hi');
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
        await req.response.close();
      }

      fakeServer.listen(handler);

      expect(DeviceLog(sl4f).error('hi'), completes);
    });
  });
}
