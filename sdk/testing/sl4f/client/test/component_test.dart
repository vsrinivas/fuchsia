// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

  test('call Search with Component not running', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Search');
      expect(body['params'], {
        'name': 'fake_name',
      });
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'NotFound', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(Component(sl4f).search('fake_name'), completion(equals(false)));
  });

  test('call Search with Component running', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Search');
      expect(body['params'], {
        'name': 'running_comp.cmx',
      });
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);

    expect(
        Component(sl4f).search('running_comp.cmx'), completion(equals(true)));
  });

  test('call List to list running component', () {
    const expectedList = [
      'component1.cmx',
      'component2.cmx',
      'component3.cmx',
    ];
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.List');
      req.response.write(jsonEncode(
          {'id': body['id'], 'result': expectedList, 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);
    expect(Component(sl4f).list(), completion(equals(expectedList)));
  });

  test('call Launch with args', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Launch');
      expect(body['params'], {
        'url': 'fuchsia-pkg://fuchsia.com/fake_url#meta/fake_url.cmx',
        'arguments': ['--arg1=arg1value', '--arg2=arg2value']
      });
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);
    expect(
        Component(sl4f)
            .launch('fake_url', ['--arg1=arg1value', '--arg2=arg2value']),
        completion(equals('Success')));
  });
  test('call Launch with no arg and component name', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Launch');
      expect(body['params'],
          {'url': 'fuchsia-pkg://fuchsia.com/fake_url#meta/fake_url.cmx'});
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);
    expect(Component(sl4f).launch('fake_url'), completion(equals('Success')));
  });
  test('call Launch with packge url', () {
    void handler(HttpRequest req) async {
      expect(req.contentLength, greaterThan(0));
      final body = jsonDecode(await utf8.decoder.bind(req).join());
      expect(body['method'], 'component_facade.Launch');
      expect(body['params'],
          {'url': 'fuchsia-pkg://fuchsia.com/fake_url#meta/fake_url.cmx'});
      req.response.write(
          jsonEncode({'id': body['id'], 'result': 'Success', 'error': null}));
      await req.response.close();
    }

    fakeServer.listen(handler);
    expect(
        Component(sl4f)
            .launch('fuchsia-pkg://fuchsia.com/fake_url#meta/fake_url.cmx'),
        completion(equals('Success')));
  });
}
