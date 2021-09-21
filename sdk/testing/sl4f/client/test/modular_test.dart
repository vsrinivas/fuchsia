// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

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
      expect(body['method'], 'modular_facade.RestartSession');
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
      expect(body['method'], 'modular_facade.StartBasemgr');
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
      expect(body['method'], 'modular_facade.StartBasemgr');
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
      expect(body['method'], 'modular_facade.LaunchMod');
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
      if (body['method'] == 'modular_facade.IsBasemgrRunning') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': called,
          'error': null,
        }));
      } else {
        expect(body['method'], 'modular_facade.StartBasemgr');
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
      if (body['method'] == 'modular_facade.IsBasemgrRunning') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': called,
          'error': null,
        }));
      } else {
        expect(body['method'], 'modular_facade.StartBasemgr');
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
      expect(body['method'], 'modular_facade.IsBasemgrRunning');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': false,
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
      expect(body['method'], 'modular_facade.IsBasemgrRunning');
      req.response.write(jsonEncode({
        'id': body['id'],
        'result': true,
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
      expect(body['method'], 'modular_facade.KillBasemgr');
      expect(body['params'], null);
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Modular(sl4f).killBasemgr(), completion(equals('Success')));
  });

  test('shutdown kills modular when it owns it', () async {
    bool killed = true;
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      if (body['method'] == 'modular_facade.IsBasemgrRunning') {
        req.response.write(jsonEncode({
          'id': body['id'],
          'result': !killed,
          'error': null,
        }));
      } else if (body['method'] == 'modular_facade.StartBasemgr') {
        expect(
          body['params'],
          isNotNull,
        );
        expect(body['params'], isEmpty);
        killed = false;
        req.response.write(
            jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      } else {
        expect(body['method'], 'modular_facade.KillBasemgr');
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

  test('modify config', () {
    const fakeConfig = '''
    {
  "basemgr": {
    "base_shell": {
      "url": "fuchsia-pkg://fuchsia.com/base_shell#meta/base_shell.cmx",
      "args": ["--base_shell_arg"]
    },
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/session#meta/session.cmx"
      }
    ],
    "session_shell_arg": true
  },
  "sessionmgr": {
    "session_agents": [
      "fuchsia-pkg://fuchsia.com/component_foo#meta/foo_a.cmx",
      "fuchsia-pkg://fuchsia.com/component_foo#meta/foo_b.cmx",
      "fuchsia-pkg://fuchsia.com/component_bar#meta/bar.cmx"
    ],
    "component_args": [
      {
        "uri": "fuchsia-pkg://fuchsia.com/component_foo#meta/foo_a.cmx",
        "args": [
          "--food=burger",
          "--drink=coffee"
        ]
      },
      {
        "uri": "fuchsia-pkg://fuchsia.com/component_bar#meta/bar.cmx",
        "args": []
      }
    ],
    "agent_service_index": [
      {
        "service_name": "some.service.a",
        "agent_url": "fuchsia-pkg://fuchsia.com/service_a#meta/service_a.cmx"
      },
      {
        "service_name": "some.service.b",
        "agent_url": "fuchsia-pkg://fuchsia.com/service_b#meta/service_b.cmx"
      }
    ]
  }
}
    ''';
    // Can modify args.
    final modified_1 =
        Modular(sl4f).updateComponentArgs(fakeConfig, RegExp(r'foo_\w'), {
      'food': 'salad',
      'desert': 'cake',
    });
    final fooArgs =
        jsonDecode(modified_1)['sessionmgr']['component_args'][0]['args'];
    expect(fooArgs[0], '--drink=coffee', reason: 'did not keep arg --drink');
    expect(fooArgs[1], '--food=salad', reason: 'did not update arg --food');
    expect(fooArgs[2], '--desert=cake', reason: 'did not add new arg --desert');
    expect(fooArgs.length, 3, reason: 'expect 3 args, got ${fooArgs.length}');

    // Can insert args for components with empty args.
    final modified_2 =
        Modular(sl4f).updateComponentArgs(modified_1, RegExp('bar'), {
      'animal': 'cat',
      'happy': null,
    });
    final barArgs =
        jsonDecode(modified_2)['sessionmgr']['component_args'][1]['args'];
    expect(barArgs[0], '--animal=cat', reason: 'did not add arg --animal');
    expect(barArgs[1], '--happy', reason: 'did not add arg --happy');
    expect(barArgs.length, 2, reason: 'expect 2 args, got ${barArgs.length}');

    // Check everything stays the same on a non-existent component
    final modified_3 =
        Modular(sl4f).updateComponentArgs(modified_2, RegExp('fake'), {
      'animal': 'cat',
      'happy': null,
    });
    final fakeArgs =
        jsonDecode(modified_3)['sessionmgr']['component_args'][1]['args'];
    expect(fakeArgs[0], '--animal=cat', reason: 'did not add arg --animal');
    expect(fakeArgs[1], '--happy', reason: 'did not add arg --happy');
    expect(fakeArgs.length, 2, reason: 'expect 2 args, got ${fakeArgs.length}');
  });
}
