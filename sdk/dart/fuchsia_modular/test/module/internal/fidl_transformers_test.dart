// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fuchsia_modular/src/module/internal/_fidl_transformers.dart';
import 'package:test/test.dart';

void main() {
  group('intent transformers', () {
    test('convertFidlIntentToIntent clones correct fields', () {
      final fidlIntent = fidl.Intent(
        action: 'my-action',
        handler: 'my-handler',
      );
      final intent = convertFidlIntentToIntent(fidlIntent);
      expect(intent.action, fidlIntent.action);
      expect(intent.handler, fidlIntent.handler);
    });

    test('convertFidlIntentToIntent handles null fidl intent parametsrs', () {
      final fidlIntent = fidl.Intent(
        action: 'my-action',
        parameters: null,
      );
      final intent = convertFidlIntentToIntent(fidlIntent);
      expect(intent.parameters, isNotNull);
    });
  });
}
