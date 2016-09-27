// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/uuid.dart';
import 'package:test/test.dart';

// Test the uuid.
void main() {
  group('Uuid', () {
    test('Base64 encoding/decoding', () {
      final String uuidBase64 = '-_erDNE0Rp65vqOvrVYaIQ==';
      final Uuid uuid = Uuid.fromBase64(uuidBase64);
      expect(uuid.toBase64(), equals(uuidBase64));
    });
  });
}
