// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: cascade_invocations

import 'package:fidl/fidl.dart';
import 'package:test/test.dart';

void main() async {
  group('encode/decode', () {
    test('encode integer bounds', () {
      final encoder = Encoder(kWireFormatDefault)..alloc(1024, 0);

      encoder.encodeInt8(-128, 0);
      encoder.encodeInt8(127, 0);
      expect(() => encoder.encodeInt8(-129, 0), throwsException);
      expect(() => encoder.encodeInt8(128, 0), throwsException);

      encoder.encodeUint8(0, 0);
      encoder.encodeUint8(255, 0);
      expect(() => encoder.encodeUint8(-1, 0), throwsException);
      expect(() => encoder.encodeUint8(256, 0), throwsException);

      encoder.encodeInt16(-32768, 0);
      encoder.encodeInt16(32767, 0);
      expect(() => encoder.encodeInt16(-32769, 0), throwsException);
      expect(() => encoder.encodeInt16(32768, 0), throwsException);

      encoder.encodeUint16(0, 0);
      encoder.encodeUint16(65535, 0);
      expect(() => encoder.encodeUint16(-1, 0), throwsException);
      expect(() => encoder.encodeUint16(65536, 0), throwsException);

      encoder.encodeInt32(-2147483648, 0);
      encoder.encodeInt32(2147483647, 0);
      expect(() => encoder.encodeInt32(-2147483649, 0), throwsException);
      expect(() => encoder.encodeInt32(2147483648, 0), throwsException);

      encoder.encodeUint32(0, 0);
      encoder.encodeUint32(4294967295, 0);
      expect(() => encoder.encodeUint32(-1, 0), throwsException);
      expect(() => encoder.encodeUint32(4294967296, 0), throwsException);
    });
  });
}
