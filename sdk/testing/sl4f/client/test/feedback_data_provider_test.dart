// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:archive/archive.dart';

import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

void main(List<String> args) {
  HttpServer fakeServer;
  Sl4f sl4f;

  setUp(() async {
    fakeServer = await HttpServer.bind('127.0.0.1', 18080);
    sl4f = Sl4f('127.0.0.1', null, 18080);
  });

  tearDown(() async {
    await fakeServer.close();
  });

  test('get bug report', () async {
    final inspect = [
      {'test': 1}
    ];
    final inspectJson = utf8.encode(jsonEncode(inspect));
    final archive = Archive()
      ..addFile(ArchiveFile('inspect.json', inspectJson.length, inspectJson));
    final zipBytes = ZipEncoder().encode(archive);

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'feedback_data_provider_facade.GetSnapshot');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': {
          'zip': base64Encode(zipBytes),
        },
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await FeedbackDataProvider(sl4f).getSnapshot();
    expect(result.inspect, equals(inspect));
  }, timeout: Timeout(Duration(minutes: 2)));
}
