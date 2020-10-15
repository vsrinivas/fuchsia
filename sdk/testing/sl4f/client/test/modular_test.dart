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

  test('call startBasemgr facade with params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.StartBasemgr');
      expect(body['params'], isNotNull);
      expect(
          body['params'],
          containsPair(
              'config',
              allOf(containsPair('basemgr', contains('base_shell')),
                  containsPair('sessionmgr', contains('session_agents')))));
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).startBasemgr('''{
      "basemgr": {
        "base_shell": {
          "url": "foo",
          "args": ["--bar"]
        }
      },
      "sessionmgr": {
        "session_agents": ["baz"]
      }
    }'''), completion(equals('Success')));
  });

  test('call startBasemgr facade with no params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.StartBasemgr');
      expect(body['params'], isNotNull);
      expect(body['params'], isEmpty);
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).startBasemgr(), completion(equals('Success')));
  });

  test('call LaunchMod facade with optional params', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'basemgr_facade.LaunchMod');
      expect(body['params'],
          {'mod_url': 'fake_url', 'mod_name': 'fake_name', 'story_name': null});
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).launchMod('fake_url', modName: 'fake_name'),
        completion(equals('Success')));
  });

  test('call boot with no config', () async {
    bool called = false;
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      if (body['method'] == 'component_facade.Search') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': 'NotFound',
          'error': null,
        }));
      } else {
        expect(body['method'], 'basemgr_facade.StartBasemgr');
        expect(body['params'], isNotNull);
        expect(body['params'], isEmpty);
        called = true;
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      }
      await req.response.close();
    }

    fakeServer.listen(handler);

    await Modular(sl4f).boot();
    expect(called, isTrue, reason: 'StartBasemgr facade not called');
  });

  test('call boot with custom config', () async {
    bool called = false;
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      if (body['method'] == 'component_facade.Search') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': 'NotFound',
          'error': null,
        }));
      } else {
        expect(body['method'], 'basemgr_facade.StartBasemgr');
        expect(body['params'], isNotNull);
        expect(
            body['params'],
            containsPair(
                'config',
                allOf(containsPair('basemgr', contains('base_shell')),
                    containsPair('sessionmgr', contains('session_agents')))));
        called = true;
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      }
      await req.response.close();
    }

    fakeServer.listen(handler);

    await Modular(sl4f).boot(config: '''{
      "basemgr": {
        "base_shell": {
          "url": "foo",
          "args": ["--bar"]
        }
      },
      "sessionmgr": {
        "session_agents": ["baz"]
      }
    }''');

    expect(called, isTrue, reason: 'StartBasemgr facade not called');
  });

  test('isRunning: no', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Search');
      expect(body['params']['name'], 'basemgr.cmx');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': 'NotFound',
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
      expect(body['method'], 'component_facade.Search');
      expect(body['params']['name'], 'basemgr.cmx');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': 'Success',
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

  test('shutdown kills modular when it owns it', () async {
    int searchCount = 0;
    bool killed = false;
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      if (body['method'] == 'component_facade.Search') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': searchCount != 1 ? 'NotFound' : 'Success',
          'error': null,
        }));
        searchCount++;
      } else if (body['method'] == 'basemgr_facade.StartBasemgr') {
        expect(
          body['params'],
          isNotNull,
        );
        expect(body['params'], isEmpty);
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      } else {
        expect(body['method'], 'basemgr_facade.KillBasemgr');
        expect(body['params'], anyOf(isNull, isEmpty));
        killed = true;
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      }
      await req.response.close();
    }

    fakeServer.listen(handler);

    final modular = Modular(sl4f);
    await modular.boot();

    expect(modular.controlsBasemgr, isTrue,
        reason: 'controlsBasemgr after boot');

    await modular.shutdown();

    expect(modular.controlsBasemgr, isFalse,
        reason: 'still controlsBasemgr after shutdown');
    expect(killed, isTrue, reason: 'did not call KillBasemgr');
  });
}
