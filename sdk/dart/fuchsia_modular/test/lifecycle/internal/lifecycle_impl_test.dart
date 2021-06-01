// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fuchsia_modular/src/lifecycle/internal/_lifecycle_impl.dart'; // ignore: implementation_imports
import 'package:fuchsia_services/services.dart';
import 'package:test/test.dart';

Future<void> terminateListener1() async {
  print('terminateListener1');
}

Future<void> terminateListener2() async {
  print('terminateListener2');
}

Future<void> throwingTerminateListener() async {
  print('throwingTerminateListener');
  throw Exception('something went wrong');
}

void main() {
  late ComponentContext context;
  late LifecycleImpl lifecycleImpl;

  setUpAll(() {
    context = ComponentContext.createAndServe();
  });

  setUp(() {
    lifecycleImpl = LifecycleImpl(outgoing: context.outgoing);
  });

  test('addTerminateListener throws for null listener', () {
    expect(() => lifecycleImpl..addTerminateListener(null),
        throwsA((const TypeMatcher<Exception>())));
  });

  test('addTerminateListener should return false when adding same handler', () {
    final host = lifecycleImpl..addTerminateListener(terminateListener1);
    expect(host.addTerminateListener(terminateListener1), false);
  });

  test('addTerminateListener successful add', () {
    final host = lifecycleImpl..addTerminateListener(terminateListener1);
    expect(host.addTerminateListener(terminateListener2), true);
  });

  test('failing terminate handler should error', () {
    print('testing 1');
    final host = lifecycleImpl
      ..addTerminateListener(expectAsync0(terminateListener1))
      ..addTerminateListener(expectAsync0(throwingTerminateListener));

    expect(host.terminate(), throwsException);
  });

  test('terminate should trigger all added listeners to execute', () {
    lifecycleImpl
      ..addTerminateListener(expectAsync0(terminateListener1))
      ..addTerminateListener(expectAsync0(terminateListener2))
      ..terminate();
  });

  test('terminate invokes the exitHandler', () async {
    int exitCode = -1;
    await LifecycleImpl(
      outgoing: context.outgoing,
      exitHandler: (c) => exitCode = c,
    ).terminate();
    expect(exitCode, 0);
  });
}
