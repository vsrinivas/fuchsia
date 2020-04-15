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

  test('call LaunchMod facade with optional params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.LaunchMod');
      expect(body['params'], {
        'mod_url': 'fake_url',
        'mod_name': 'fake_name',
        'story_name': null,
        'focus_mod': true,
        'focus_story': null,
      });
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(
        Modular(sl4f)
            .launchMod('fake_url', modName: 'fake_name', focusMod: true),
        completion(equals('Success')));
  });

  test('isRunning: no', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['basemgr.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': [],
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).isRunning, completion(equals(false)));
  });

  test('isRunning: yes', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'diagnostics_facade.SnapshotInspect');
      expect(body['params']['selectors'], ['basemgr.cmx:root']);
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': [
          {
            'contents': {
              'root': {'some_prop': 1}
            }
          }
        ],
        'error': null,
      }));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).isRunning, completion(equals(true)));
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
