// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart'; // ignore: implementation_imports
import 'package:test/test.dart';

void main() {
  setupLogger(name: 'fuchsia-services-test');
  // We don't use createAndServe here because the test case gets no outgoing directory handle,
  // so it would crash.
  final context = ComponentContext.create();

  group('ComponentContext: ', () {
    test('createAndServe does not return null instance', () {
      expect(context, isNotNull);
    });

    test('create throws an error when called twice', () {
      // TODO(https://fxbug.dev/71711): Figure out why `dart analyze` complains
      // about this.
      // ignore: unnecessary_lambdas
      expect(() => ComponentContext.create(), throwsException);
    });

    test('createAndServe throws an error when called twice', () {
      // TODO(https://fxbug.dev/71711): Figure out why `dart analyze` complains
      // about this.
      // ignore: unnecessary_lambdas
      expect(() => ComponentContext.createAndServe(), throwsException);
    });
  });
}
