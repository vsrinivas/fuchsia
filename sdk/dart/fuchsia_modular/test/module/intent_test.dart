// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_modular/src/module/intent.dart'; // ignore: implementation_imports

import 'package:test/test.dart';

void main() {
  group('intent constructors', () {
    test('intent with handler sets the handler', () {
      final intent = Intent(action: '', handler: 'my-handler');
      expect(intent.handler, 'my-handler');
    });
  });
}
