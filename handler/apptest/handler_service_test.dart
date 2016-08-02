// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:handler/handler.dart';
import 'package:handler/session.dart' show Session;
import 'package:modular_core/log.dart';
import 'package:modular/modular/handler.mojom.dart' as mojom;
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart' show parseRecipe;
import 'package:parser/recipe.dart';
import 'package:mojo/core.dart';
import 'package:test/test.dart';

import '../mojo/handler_service.dart';

const String manifest1 = """
verb: v1
input: p1
url: http://tq.io/u1

use:
 - v1: http://tq.io/v1
 - p1: http://tq.io/p1
""";

const String manifest2 = """
verb: v1
input: p2
url: http://tq.io/u2

use:
 - v1: http://tq.io/v1
 - p2: http://tq.io/p2
""";

const String manifest3 = """
verb: v2
input: p1

use:
 - v2: http://tq.io/v2
 - p1: http://tq.io/p1
""";

const String recipeText = """
title: foo

recipe:
 - verb: v2
   input: p1

use:
 - v1: http://tq.io/v1
 - v2: http://tq.io/v2
 - p1: http://tq.io/p1
 - p2: http://tq.io/p2
""";

typedef void Complete();
typedef void CompleteStatus(HandlerStatus status);

void testHandlerService() {
  Handler handler;
  mojom.HandlerServiceProxy proxy;
  mojom.HandlerServiceStub stub;

  setUp(() {
    final List<Manifest> manifests = [manifest1, manifest2, manifest3]
        .map((String s) => new Manifest.parseYamlString(s))
        .toList();
    handler = new Handler(manifests: manifests);
    final MojoMessagePipe pipe = new MojoMessagePipe();

    stub = new mojom.HandlerServiceStub.fromEndpoint(pipe.endpoints[0]);
    stub.impl = new HandlerServiceImpl(handler, null);
    proxy = new mojom.HandlerServiceProxy.fromEndpoint(pipe.endpoints[1]);
    proxy.ctrl.errorFuture.catchError((final dynamic error) {
      log('HandlerTest').info('Error in handler connection: $error');
    });
  });

  tearDown(() {
    proxy.close();
    stub.close();
  });

  /// Wrap a callback-based handler mojo call to be async.
  Future<Null> _complete(void callHandler(Complete complete)) {
    final Completer completer = new Completer();
    callHandler(completer.complete);
    return completer.future;
  }

  /// Wrap a callback-based handler mojo call that returns [HandlerStatus] to be
  /// async.
  Future<HandlerStatus> _status(void callHandler(CompleteStatus complete)) {
    final Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    callHandler((final HandlerStatus status) => completer.complete(status));
    return completer.future;
  }

  group('HandlerService', () {
    final Verb v1 = new Verb(new Label.fromUri(new Uri.http('tq.io', 'v1')));
    final Verb v2 = new Verb(new Label.fromUri(new Uri.http('tq.io', 'v2')));
    final Property p1 =
        new Property([new Label.fromUri(new Uri.http('tq.io', 'p1'))]);
    final Property p2 =
        new Property([new Label.fromUri(new Uri.http('tq.io', 'p2'))]);
    final Uri u1 = new Uri.http('tq.io', 'u1');
    final Uri u2 = new Uri.http('tq.io', 'u2');
    final String jsonStringStep1 =
        new Step(null, v1, [new PathExpr.single(p1)], [], [], [], null)
            .toJsonString();
    final String jsonStringStep2 =
        new Step(null, v1, [new PathExpr.single(p2)], [], [], [], null)
            .toJsonString();
    final String jsonStringStep3 =
        new Step(null, v2, [new PathExpr.single(p1)], [], [], [], null)
            .toJsonString();

    test('Add and remove steps', () async {
      final Recipe recipe = parseRecipe(recipeText);
      final Session session = await handler.createSession(recipe);
      session.start();

      Future<HandlerStatus> _updateSession(final List<String> jsonAddSteps,
              final List<String> jsonRemoveSteps) =>
          _status((CompleteStatus complete) {
            proxy.updateSession(
                session.id.toBase64(), jsonAddSteps, jsonRemoveSteps, complete);
          });

      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(session.modules[0].manifest.url, isNull);

      // Add step 1.
      HandlerStatus status = await _updateSession([jsonStringStep1], []);
      expect(status, equals(HandlerStatus.ok));
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(u1));

      // Add step 1 again.
      status = await _updateSession([jsonStringStep1], []);
      expect(status, equals(HandlerStatus.ok));
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(u1));

      // Add step 2.
      status = await _updateSession([jsonStringStep2], []);
      expect(status, equals(HandlerStatus.ok));
      expect(session.recipe.steps.length, equals(3));
      expect(session.modules.length, equals(3));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(u1));
      expect(session.modules[2].manifest.url, equals(u2));

      // Remove step 1.
      status = await _updateSession([], [jsonStringStep1]);
      expect(status, equals(HandlerStatus.ok));
      expect(session.recipe.steps.length, equals(2));
      expect(session.modules.length, equals(2));
      expect(session.modules[0].manifest.url, isNull);
      expect(session.modules[1].manifest.url, equals(u2));
    });

    test('Swap steps', () async {
      final Recipe recipe = parseRecipe(recipeText);
      final Session session = await handler.createSession(recipe);
      session.start();

      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(session.modules[0].manifest.url, isNull);

      final HandlerStatus status = await _status((CompleteStatus complete) {
        proxy.updateSession(session.id.toBase64(), [jsonStringStep2],
            [jsonStringStep3], complete);
      });

      // Swap step 3 with step 2.
      expect(status, equals(HandlerStatus.ok));
      expect(session.recipe.steps.length, equals(1));
      expect(session.modules.length, equals(1));
      expect(session.modules[0].manifest.url, equals(u2));
    });

    test('Fork session', () async {
      final Recipe recipe = parseRecipe(recipeText);
      final Session session = await handler.createSession(recipe);
      session.start();

      // Unknown session ID.
      await _complete((Complete complete) {
        proxy.forkSession(new Uuid.random().toBase64(),
            (final HandlerStatus status, final String sessionId) {
          expect(status, equals(HandlerStatus.invalidSessionId));
          expect(sessionId, isNull);
          complete();
        });
      });

      // Fork |session|.
      String forkId;
      await _complete((Complete complete) {
        proxy.forkSession(session.id.toBase64(),
            (final HandlerStatus status, final String sessionId) {
          expect(status, equals(HandlerStatus.ok));
          expect(sessionId, isNotNull);
          forkId = sessionId;
          complete();
        });
      });

      // Stop the session. Restoring it should fail.
      HandlerStatus status = await _status((CompleteStatus complete) {
        proxy.stopSession(forkId, complete);
      });
      expect(status, equals(HandlerStatus.ok));

      status = await _status((CompleteStatus complete) {
        proxy.restoreSession(forkId, complete);
      });
      expect(status, equals(HandlerStatus.invalidSessionId));
    });
  });
}
