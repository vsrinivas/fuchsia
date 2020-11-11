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

fidl.Message _encode<T>(fidl.Encoder encoder, fidl.FidlType<T> type, T value) {
  encoder.alloc(type.encodingInlineSize(encoder));
  type.encode(encoder, value, 0);
  return encoder.message;
}

Uint8List _getMessageBytes(fidl.Message message) {
  return Uint8List.view(message.data.buffer, 0, message.data.lengthInBytes);
}

T _decode<T>(fidl.Decoder decoder, fidl.FidlType<T> type, Uint8List bytes,
    List<Handle> handles) {
  decoder
    ..data = ByteData.view(bytes.buffer, 0, bytes.length)
    ..handles = handles
    ..claimMemory(type.decodingInlineSize(decoder));
  return type.decode(decoder, 0);
}

typedef FactoryFromHandles<T> = T Function(List<Handle> handleDefs);

class EncodeSuccessCase<T> {
  EncodeSuccessCase(this.encoder, this.input, this.type, this.bytes,
      {this.handles = const []});

  final fidl.Encoder encoder;
  final T input;
  final fidl.FidlType<T> type;
  final Uint8List bytes;
  final List<Handle> handles;

  static void run<T>(fidl.Encoder encoder, String name, T input,
      fidl.FidlType<T> type, Uint8List bytes) {
    group(name, () {
      EncodeSuccessCase(encoder, input, type, bytes)._checkEncode();
    });
  }

  static void runWithHandles<T>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T> type,
      Uint8List bytes,
      List<HandleSubtype> subtypes,
      List<int> handleOrder) {
    final handleDefs = createHandles(subtypes);
    final expectedHandles = _reorderList(handleDefs, handleOrder);
    final testCase = EncodeSuccessCase(
        encoder, inputFactory(handleDefs), type, bytes,
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
      final message = _encode(encoder, type, input);
      expect(_getMessageBytes(message), equals(bytes));
      expect(message.handles, equals(handles));
    });
  }
}

class DecodeSuccessCase<T> {
  DecodeSuccessCase(this.decoder, this.input, this.type, this.bytes,
      {this.handles = const []});

  final fidl.Decoder decoder;
  final T input;
  final fidl.FidlType<T> type;
  final Uint8List bytes;
  final List<Handle> handles;

  static void run<T>(fidl.Decoder decoder, String name, T input,
      fidl.FidlType<T> type, Uint8List bytes) {
    group(name, () {
      DecodeSuccessCase(decoder, input, type, bytes)._checkDecode();
    });
  }

  static void runWithHandles<T>(
      fidl.Decoder decoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T> type,
      Uint8List bytes,
      List<HandleSubtype> subtypes,
      List<int> handleOrder,
      // this parameter can be made non-optional once it is emitted in GIDL
      [List<int> unusedHandles = const []]) {
    final handleDefs = createHandles(subtypes);
    final inputHandles = _reorderList(handleDefs, handleOrder);
    final testCase = DecodeSuccessCase(
        decoder, inputFactory(handleDefs), type, bytes,
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
      expect(_decode(decoder, type, bytes, handles), equals(input));
    });
  }
}

typedef Factory<T> = T Function();

class EncodeFailureCase<T> {
  EncodeFailureCase(this.encoder, this.inputFactory, this.type, this.code);

  final fidl.Encoder encoder;
  final Factory<T> inputFactory;
  final fidl.FidlType<T> type;
  final fidl.FidlErrorCode code;

  static void run<T>(fidl.Encoder encoder, String name, Factory<T> inputFactory,
      fidl.FidlType<T> type, fidl.FidlErrorCode code) {
    group(name, () {
      EncodeFailureCase(encoder, inputFactory, type, code)._checkEncodeFails();
    });
  }

  static void runWithHandles<T>(
      fidl.Encoder encoder,
      String name,
      FactoryFromHandles<T> inputFactory,
      fidl.FidlType<T> type,
      fidl.FidlErrorCode code,
      List<HandleSubtype> subtypes) {
    final handleDefs = createHandles(subtypes);
    group(name, () {
      EncodeFailureCase(encoder, () => inputFactory(handleDefs), type, code)
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
        _encode(encoder, type, input);
      },
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}

class DecodeFailureCase<T> {
  DecodeFailureCase(
      this.decoder, this.type, this.bytes, this.code, this.handles);

  final fidl.Decoder decoder;
  final fidl.FidlType<T> type;
  final Uint8List bytes;
  final fidl.FidlErrorCode code;
  final List<Handle> handles;

  /// run supports DecodeFailureCases both with and without handles, depending on whether
  /// the optional parameters are provided.
  // The two optional parameters can be merged into a single List<HandleSubtype> in the
  // desired order, but it is simpler to reorder here than having special handling
  // for decode failures in the GIDL backend.
  static void run<T>(fidl.Decoder decoder, String name, fidl.FidlType<T> type,
      Uint8List bytes, fidl.FidlErrorCode code,
      [List<HandleSubtype> subtypes = const [],
      List<int> handleOrder = const []]) {
    final handles = _reorderList(createHandles(subtypes), handleOrder);
    group(name, () {
      DecodeFailureCase(decoder, type, bytes, code, handles)
          ._checkDecodeFails();
      test('handles are closed', () {
        expect(handles.map(isHandleClosed), equals(handles.map((_) => true)));
      });
    });
  }

  void _checkDecodeFails() {
    test('decode fails', () {
      expect(
          () => _decode(decoder, type, bytes, handles),
          throwsA(const TypeMatcher<fidl.FidlError>()
              .having((e) => e.code, 'code', equals(code))));
    });
  }
}
