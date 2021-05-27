// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';
import 'package:fidl/fidl.dart' as fidl;
import 'package:zircon/zircon.dart';

import 'handles.dart';

// ignore: avoid_classes_with_only_static_members
abstract class Encoders {
  // ignore: prefer_constructors_over_static_methods
  static fidl.Encoder get v1 {
    return fidl.Encoder();
  }
}

// ignore: avoid_classes_with_only_static_members
abstract class Decoders {
  // ignore: prefer_constructors_over_static_methods
  static fidl.Decoder get v1 {
    return fidl.Decoder.fromRawArgs(null, []);
  }
}

/// Returns list `result` where `result[i] == data[order[i]]`. Assumes all
/// indices are valid.
List<T> _reorderList<T>(List<T> data, List<int> order) =>
    order.map((index) => data[index]).toList();

fidl.OutgoingMessage _encode<T, I extends Iterable<T>>(
    fidl.FidlType<T, I> type, T value) {
  final fidl.Encoder encoder = fidl.Encoder()..encodeMessageHeader(0, 0);
  fidl.MemberType member = fidl.MemberType(
    type: type,
    offset: 0,
  );
  fidl.encodeMessage(encoder, type.encodingInlineSize(), member, value);
  return encoder.message;
}

/// Ignores the 16-byte header to return the bytes of only the message body.
Uint8List _getMessageBodyBytes(fidl.OutgoingMessage message) {
  return Uint8List.view(message.data.buffer, fidl.kMessageHeaderSize,
      message.data.lengthInBytes - fidl.kMessageHeaderSize);
}

T _decode<T, I extends Iterable<T>>(
    fidl.FidlType<T, I> type, Uint8List bytes, List<Handle> handles) {
  // The fidl.decodeMessage function assumes that the passed in FIDL message has
  // a header, while the passed in byte array does not.  This builder prepends
  // an empty placeholder "header" to the byte array, allowing it be properly
  // decoded by the function during tests.
  BytesBuilder input = BytesBuilder(copy: false)
    ..add(Uint8List(fidl.kMessageHeaderSize))
    ..add(bytes);
  // TODO(fxbug.dev/41920) Use GIDL-specified handle rights and type here.
  fidl.IncomingMessage message = fidl.IncomingMessage(
      ByteData.view(input.toBytes().buffer, 0, input.length),
      handles
          .map((handle) =>
              HandleInfo(handle, ZX.OBJ_TYPE_NONE, ZX.RIGHT_SAME_RIGHTS))
          .toList());
  fidl.MemberType member = fidl.MemberType(
    type: type,
    offset: 0,
  );
  return fidl.decodeMessage(message, type.decodingInlineSize(), member);
}

typedef FactoryFromHandles<T> = T Function(List<Handle> handleDefs);

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
      EncodeSuccessCase(input, type, bytes)._checkEncode();
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      Uint8List bytes,
      List<HandleSubtype> subtypes,
      List<int> handleOrder) {
    final handleDefs = createHandles(subtypes);
    final expectedHandles = _reorderList(handleDefs, handleOrder);
    final testCase = EncodeSuccessCase(inputFactory(handleDefs), type, bytes,
        handles: expectedHandles);
    group(name, () {
      testCase._checkEncode();
      tearDown(() {
        closeHandles(testCase.handles);
      });
    });
  }

  void _checkEncode() {
    test('encode', () {
      final message = _encode(type, input);
      expect(_getMessageBodyBytes(message), equals(bytes));
      expect(message.handles, equals(handles));
    });
  }
}

class DecodeSuccessCase<T, I extends Iterable<T>> {
  DecodeSuccessCase(this.input, this.type, this.bytes,
      {this.handles = const []});

  final T input;
  final fidl.FidlType<T, I> type;
  final Uint8List bytes;
  final List<Handle> handles;

  static void run<T, I extends Iterable<T>>(
      String name, T input, fidl.FidlType<T, I> type, Uint8List bytes) {
    group(name, () {
      DecodeSuccessCase(input, type, bytes)._checkDecode();
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      Uint8List bytes,
      List<HandleSubtype> subtypes,
      List<int> handleOrder,
      // this parameter can be made non-optional once it is emitted in GIDL
      [List<int> unusedHandles = const []]) {
    final handleDefs = createHandles(subtypes);
    final inputHandles = _reorderList(handleDefs, handleOrder);
    final testCase = DecodeSuccessCase(inputFactory(handleDefs), type, bytes,
        handles: inputHandles);
    group(name, () {
      testCase._checkDecode();
      test('unused handles are closed', () {
        expect(_reorderList(handleDefs, unusedHandles).map(isHandleClosed),
            equals(unusedHandles.map((_) => true)));
      }, skip: unusedHandles.isEmpty ? null : 'no unused handles');

      tearDown(() {
        closeHandles(Set.from(inputHandles)
            .difference(Set.from(unusedHandles))
            .toList()
            .cast<Handle>());
      });
    });
  }

  void _checkDecode() {
    test('decode', () {
      expect(_decode(type, bytes, handles), equals(input));
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
      EncodeFailureCase(inputFactory, type, code)._checkEncodeFails();
    });
  }

  static void runWithHandles<T, I extends Iterable<T>>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T, I> type,
      fidl.FidlErrorCode code,
      List<HandleSubtype> subtypes) {
    final handleDefs = createHandles(subtypes);
    group(name, () {
      EncodeFailureCase(() => inputFactory(handleDefs), type, code)
          ._checkEncodeFails();
      test('handles are closed', () {
        expect(handleDefs.map(isHandleClosed),
            equals(handleDefs.map((_) => true)));
      });
    });
  }

  void _checkEncodeFails() {
    test('encode fails', () {
      expect(() {
        final input = inputFactory();
        _encode(type, input);
      },
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}

class DecodeFailureCase<T, I extends Iterable<T>> {
  DecodeFailureCase(this.type, this.bytes, this.code, this.handles);

  final fidl.FidlType<T, I> type;
  final Uint8List bytes;
  final fidl.FidlErrorCode code;
  final List<Handle> handles;

  /// run supports DecodeFailureCases both with and without handles, depending on whether
  /// the optional parameters are provided.
  // The two optional parameters can be merged into a single List<HandleSubtype> in the
  // desired order, but it is simpler to reorder here than having special handling
  // for decode failures in the GIDL backend.
  static void run<T, I extends Iterable<T>>(String name,
      fidl.FidlType<T, I> type, Uint8List bytes, fidl.FidlErrorCode code,
      [List<HandleSubtype> subtypes = const [],
      List<int> handleOrder = const []]) {
    final handles = _reorderList(createHandles(subtypes), handleOrder);
    group(name, () {
      DecodeFailureCase(type, bytes, code, handles)._checkDecodeFails();
      test('handles are closed', () {
        expect(handles.map(isHandleClosed), equals(handles.map((_) => true)));
      });
    });
  }

  void _checkDecodeFails() {
    test('decode fails', () {
      expect(
          () => _decode(type, bytes, handles),
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}
