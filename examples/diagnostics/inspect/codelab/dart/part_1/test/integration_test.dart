// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:inspect_codelab_shared/codelab_environment.dart';
import 'package:test/test.dart';

void main() {
  CodelabEnvironment env;

  Future<fidl_codelab.ReverserProxy> startComponentAndConnect({
    bool includeFizzbuzz = false,
  }) async {
    if (includeFizzbuzz) {
      await env.startFizzBuzz();
    }

    const serverName = 'inspect-dart-codelab-part-1';
    const reverserUrl =
        'fuchsia-pkg://fuchsia.com/$serverName#meta/$serverName.cmx';
    return await env.startReverser(reverserUrl);
  }

  setUp(() async {
    env = CodelabEnvironment();
    await env.create();
  });

  tearDown(() async {
    env.dispose();
  });

  test('start with fizzbuzz', () async {
    // CODELAB: uncomment when it works.
    // final reverser =
    await startComponentAndConnect(includeFizzbuzz: true);
    // final result = await reverser.reverse('hello');
    // expect(result, equals('olleh'));
  });

  test('start without fizzbuzz', () async {
    // CODELAB: uncomment when it works.
    // final reverser =
    await startComponentAndConnect();
    // final result = await reverser.reverse('hello');
    // expect(result, equals('olleh'));
  });
}
