// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io';

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

  test('snapshot inspect', () async {
    final selectors = ['test.cmx:root', 'other.cmx:root/node:prop'];
    final expectedHierarchies = [
      {
        'moniker': 'test.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'a': 1}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      },
      {
        'moniker': 'other.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'prop': 2}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      }
    ];

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], selectors);
      expect(body['params']['service_name'],
          'fuchsia.diagnostics.ArchiveAccessor');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': expectedHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshot(selectors);
    expect(result, equals(expectedHierarchies));
  });

  test('snapshot inspect all', () async {
    final expectedHierarchies = [
      {
        'moniker': 'test.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'a': 1}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      },
      {
        'moniker': 'other.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'prop': 2}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      }
    ];

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], []);
      expect(body['params']['service_name'],
          'fuchsia.diagnostics.FeedbackArchiveAccessor');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': expectedHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result =
        await Inspect(sl4f).snapshotAll(pipeline: InspectPipeline.feedback);
    expect(result, equals(expectedHierarchies));
  });

  test('snapshot inspect root', () async {
    final resultHierarchies = [
      {
        'moniker': 'test.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'a': 1}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      },
      {
        'moniker': 'other.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': {
          'root': {'prop': 2}
        },
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      }
    ];

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['test.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': resultHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshotRoot('test.cmx');
    expect(result, equals({'a': 1}));
  });

  test('snapshot inspect root no hierarchy', () async {
    const resultHierarchies = [];

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['test.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': resultHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshotRoot('test.cmx');
    expect(result, isNull);
  });

  test('snapshot inspect root no payload', () async {
    final resultHierarchies = [
      {
        'moniker': 'test.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': null,
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      },
      {
        'moniker': 'other.cmx',
        'version': 1,
        'data_source': 'Inspect',
        'payload': null,
        'metadata': {
          'timestamp': 123456,
          'errors': null,
          'filename': 'test_file_plz_ignore.inspect'
        }
      }
    ];

    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['test.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': resultHierarchies,
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    final result = await Inspect(sl4f).snapshotRoot('test.cmx');
    expect(result, isNull);
  });
}
