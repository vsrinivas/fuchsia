// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl/fidl.dart' as fidl;
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

import 'handles.dart';

// ignore: avoid_classes_with_only_static_members
abstract class Encoders {
  // ignore: prefer_constructors_over_static_methods
  static fidl.Encoder get v1 {
    return fidl.Encoder(fidl.WireFormat.v1);
  }

  // ignore: prefer_constructors_over_static_methods
  static fidl.Encoder get v2 {
    return fidl.Encoder(fidl.WireFormat.v2);
  }
}

/// Returns list `result` where `result[i] == data[order[i]]`. Assumes all
/// indices are valid.
List<T> _reorderList<T>(List<T> data, List<int> order) =>
    order.map((index) => data[index]).toList();

fidl.OutgoingMessage _encode<T, I extends Iterable<T>>(
    fidl.Encoder encoder, fidl.FidlType<T, I> type, T value) {
  encoder.encodeMessageHeader(0, 0);
  fidl.MemberType member = fidl.MemberType(
    type: type,
    offsetV1: 0,
    offsetV2: 0,
  );
  fidl.encodeMessage(
      encoder, type.inlineSize(encoder.wireFormat), member, value);
  return encoder.message;
}

/// Ignores the 16-byte header to return the bytes of only the message body.
Uint8List _getMessageBodyBytes(fidl.OutgoingMessage message) {
  return Uint8List.view(message.data.buffer, fidl.kMessageHeaderSize,
      message.data.lengthInBytes - fidl.kMessageHeaderSize);
}

T _decode<T, I extends Iterable<T>>(fidl.WireFormat wireFormat,
    fidl.FidlType<T, I> type, Uint8List bytes, List<HandleInfo> handleInfos) {
  // The fidl.decodeMessage function assumes that the passed in FIDL message has
  // a header, while the passed in byte array does not.  This builder prepends
  // an empty placeholder "header" to the byte array, allowing it be properly
  // decoded by the function during tests.
  BytesBuilder messageBytesBuilder = BytesBuilder(copy: false)
    ..add(Uint8List(fidl.kMessageHeaderSize))
    ..add(bytes);
  ByteData messageBytes = ByteData.view(
      messageBytesBuilder.toBytes().buffer, 0, messageBytesBuilder.length);
  if (wireFormat == fidl.WireFormat.v2) {
    // Mark that the message contains wire format v2 bytes.
    messageBytes.setUint8(4, 2);
  }
  fidl.IncomingMessage message =
      fidl.IncomingMessage(messageBytes, handleInfos);
  fidl.MemberType member = fidl.MemberType(
    type: type,
    offsetV1: 0,
    offsetV2: 0,
  );
  return fidl.decodeMessage(message, type.inlineSize(wireFormat), member);
}

typedef FactoryFromHandles<T> = T Function(List<Handle> handles);

class EncodeSuccessCase<T, I extends Iterable<T>> {
  EncodeSuccessCase(this.input, this.type, this.bytes,
      {this.handles = const []});

  final T input;
  final fidl.FidlType<T, I> type;
  final Uint8List bytes;
  final List<Handle> handles;

  static void run<T, I extends Iterable<T>>(fidl.Encoder encoder, String name,
      T input, fidl.FidlType<T, I> type, Uint8List bytes) {
    group(name, () {
      EncodeSuccessCase(input, type, bytes)._checkEncode(encoder);
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      Uint8List bytes,
      List<HandleDef> handleDefs,
      List<int> handleOrder) {
    final handles = createHandles(handleDefs);
    final expectedHandles = _reorderList(handles, handleOrder);
    final testCase = EncodeSuccessCase(inputFactory(handles), type, bytes,
        handles: expectedHandles);
    group(name, () {
      testCase._checkEncode(encoder);
      tearDown(() {
        closeHandles(testCase.handles);
      });
    });
  }

  void _checkEncode(fidl.Encoder encoder) {
    test('encode', () {
      final message = _encode(encoder, type, input);
      expect(_getMessageBodyBytes(message), equals(bytes));
      expect(message.handleDispositions.map((h) => h.handle).toList(),
          equals(handles));
    });
  }
}

class DecodeSuccessCase<T, I extends Iterable<T>> {
  DecodeSuccessCase(this.wireFormat, this.input, this.type, this.bytes,
      {this.handleInfos = const []});

  final fidl.WireFormat wireFormat;
  final T input;
  final fidl.FidlType<T, I> type;
  final Uint8List bytes;
  final List<HandleInfo> handleInfos;

  static void run<T, I extends Iterable<T>>(
      String name,
      fidl.WireFormat wireFormat,
      T input,
      fidl.FidlType<T, I> type,
      Uint8List bytes) {
    group(name, () {
      DecodeSuccessCase(wireFormat, input, type, bytes)._checkDecode();
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      String name,
      fidl.WireFormat wireFormat,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      Uint8List bytes,
      List<HandleDef> handleDefs,
      List<int> handleOrder,
      // this parameter can be made non-optional once it is emitted in GIDL
      [List<int> unusedHandles = const []]) {
    final handleInfos = createHandleInfos(handleDefs);
    final handles = handleInfos.map((handleInfo) => handleInfo.handle).toList();
    final inputHandles = _reorderList(handleInfos, handleOrder);
    final testCase = DecodeSuccessCase(
        wireFormat, inputFactory(handles), type, bytes,
        handleInfos: inputHandles);
    group(name, () {
      testCase._checkDecode();
      test('unused handles are closed', () {
        expect(_reorderList(handles, unusedHandles).map(isHandleClosed),
            equals(unusedHandles.map((_) => true)));
      }, skip: unusedHandles.isEmpty ? null : 'no unused handles');

      tearDown(() {
        closeHandles(Set.from(handles)
            .difference(Set.from(unusedHandles))
            .toList()
            .cast<Handle>());
      });
    });
  }

  void _checkDecode() {
    test('decode', () {
      expect(_decode(wireFormat, type, bytes, handleInfos), equals(input));
    });
  }
}

typedef Factory<T> = T Function();

class EncodeFailureCase<T, I extends Iterable<T>> {
  EncodeFailureCase(this.inputFactory, this.type, this.code);

  final Factory<T> inputFactory;
  final fidl.FidlType<T, I> type;
  final fidl.FidlErrorCode code;

  static void run<T, I extends Iterable<T>>(
      fidl.Encoder encoder,
      String name,
      Factory<T> inputFactory,
      fidl.FidlType<T, I> type,
      fidl.FidlErrorCode code) {
    group(name, () {
      EncodeFailureCase(inputFactory, type, code)._checkEncodeFails(encoder);
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      fidl.FidlErrorCode code,
      List<HandleDef> handleDefs) {
    final handles = createHandles(handleDefs);
    group(name, () {
      EncodeFailureCase(() => inputFactory(handles), type, code)
          ._checkEncodeFails(encoder);
      test('handles are closed', () {
        expect(handles.map(isHandleClosed), equals(handles.map((_) => true)));
      });
    });
  }

  void _checkEncodeFails(fidl.Encoder encoder) {
    test('encode fails', () {
      expect(() {
        final input = inputFactory();
        _encode(encoder, type, input);
      },
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}

class DecodeFailureCase<T, I extends Iterable<T>> {
  DecodeFailureCase(
      this.wireFormat, this.type, this.bytes, this.code, this.handleInfos);

  final fidl.WireFormat wireFormat;
  final fidl.FidlType<T, I> type;
  final Uint8List bytes;
  final fidl.FidlErrorCode code;
  final List<HandleInfo> handleInfos;

  /// run supports DecodeFailureCases both with and without handles, depending on whether
  /// the optional parameters are provided.
  // The two optional parameters can be merged into a single List<HandleSubtype> in the
  // desired order, but it is simpler to reorder here than having special handling
  // for decode failures in the GIDL backend.
  static void run<T, I extends Iterable<T>>(
      String name,
      fidl.WireFormat wireFormat,
      fidl.FidlType<T, I> type,
      Uint8List bytes,
      fidl.FidlErrorCode code,
      [List<HandleDef> handleDefs = const [],
      List<int> handleOrder = const []]) {
    final handleInfos =
        _reorderList(createHandleInfos(handleDefs), handleOrder);
    final handles = handleInfos.map((handleInfo) => handleInfo.handle).toList();
    group(name, () {
      DecodeFailureCase(wireFormat, type, bytes, code, handleInfos)
          ._checkDecodeFails();
      test('handles are closed', () {
        expect(handles.map(isHandleClosed), equals(handles.map((_) => true)));
      });
    });
  }

  void _checkDecodeFails() {
    test('decode fails', () {
      expect(
          () => _decode(wireFormat, type, bytes, handleInfos),
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}
