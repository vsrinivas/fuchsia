// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_modular/src/internal/_component_context.dart'; // ignore: implementation_imports
import 'package:test/test.dart';

void main() {
  group('ComponentContext:', () {
    test('getComponentContext does not return null instance', () {
      expect(getComponentContext(), isNotNull);
    });

    test('getComponentContext returns the same instance', () {
      expect(getComponentContext(), getComponentContext());
    });
  });
}
