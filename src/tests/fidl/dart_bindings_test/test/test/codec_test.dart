// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: cascade_invocations

import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() async {
  ExampleXunion xunion;

  group('encode/decode', () {
    setUpAll(() async {
      ChannelPair pair = ChannelPair();
      expect(pair.status, equals(ZX.OK));
      pair.second.close();

      xunion = ExampleXunion.withWithHandle(
          NumberHandleNumber(n1: 1, h: pair.first.handle, n2: 2));
    });

    // Test decoding a handle that has more rights than are specified in FIDL.
    // The handle rights should be reduced to the set of rights specified in
    // FIDL.
    //
    // Normally this would be implemented in GIDL, but because
    // zx_object_get_info doesn't exist in dart, a custom test is needed.
    test('handle with extra rights', () async {
      void reduceRightsViaFidl(Function check) {
        ChannelPair channelPair = ChannelPair();
        final defaultRights = ChannelWithDefaultRights(c: channelPair.first);

        var encoder = Encoder(kWireFormatDefault)..alloc(8, 0);
        kChannelWithDefaultRights_Type.encode(encoder, defaultRights, 0, 1);

        final decoder =
            Decoder(IncomingMessage.fromOutgoingMessage(encoder.message))
              ..claimMemory(8, 0);
        final reducedRights =
            kChannelWithReducedRights_Type.decode(decoder, 0, 1);

        // The dart zircon library replaces handles with invalid handles on
        // failure.
        expect(reducedRights.c.handle, isNot(equals(Handle.invalid())));
        check(reducedRights.c.handle);

        channelPair.first.close();
        channelPair.second.close();
      }

      // The reduceRightsViaFidl function is necessary because each expect case
      // is destructive - either the handle becomes invalid or the rights are
      // changed.
      reduceRightsViaFidl((handle) {
        expect(handle.replace(ZX.DEFAULT_CHANNEL_RIGHTS),
            equals(Handle.invalid()));
      });
      reduceRightsViaFidl((handle) {
        expect(
            handle.replace(ZX.RIGHT_TRANSFER), isNot(equals(Handle.invalid())));
      });
    });

    // TODO(fxbug.dev/56687): test in GIDL
    test('unknown ordinal flexible with handles', () async {
      var encoder = Encoder(kWireFormatDefault)..alloc(24, 0);
      kExampleXunion_Type.encode(encoder, xunion, 0, 1);

      // overwrite the ordinal to be unknown
      encoder.encodeUint64(0x1234, 0);

      final decoder =
          Decoder(IncomingMessage.fromOutgoingMessage(encoder.message))
            ..claimMemory(24, 0);
      ExampleXunion unknownXunion = kExampleXunion_Type.decode(decoder, 0, 1);
      UnknownRawData actual = unknownXunion.$data;
      final expectedData = Uint8List.fromList([
        0x01, 0x00, 0x00, 0x00, // n1
        0xFF, 0xFF, 0xFF, 0xFF, // kHandlePresent
        0x02, 0x00, 0x00, 0x00, // n2
        0x00, 0x00, 0x00, 0x00, // padding
      ]);
      expect(actual.data, equals(expectedData));
      expect(actual.handles.length, equals(1));

      encoder = Encoder(kWireFormatDefault)..alloc(24, 0);
      kExampleXunion_Type.encode(encoder, unknownXunion, 0, 1);

      expect(encoder.message.data.lengthInBytes, 40);
      final bytes = encoder.message.data.buffer.asUint8List(0, 40);
      expect(
          bytes,
          equals([
            0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ordinal + padding
            0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // num bytes/handles
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // PRESENT
            0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, // n1 + h
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // n2 + paddding
          ]));
      expect(encoder.message.handleDispositions.length, equals(1));
      encoder.message.handleDispositions[0].handle.close();
    });

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
