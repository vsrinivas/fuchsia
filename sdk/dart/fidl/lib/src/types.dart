// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:zircon/zircon.dart';

import 'bits.dart';
import 'codec.dart';
import 'enum.dart';
import 'error.dart';
import 'interface.dart';
import 'struct.dart';
import 'table.dart';
import 'unknown_data.dart';
import 'xunion.dart';

// ignore_for_file: public_member_api_docs
// ignore_for_file: always_specify_types

void _throwIfNotNullable(bool nullable) {
  if (!nullable) {
    throw FidlError('Found null for a non-nullable type',
        FidlErrorCode.fidlNonNullableTypeWithNullValue);
  }
}

void _throwIfExceedsLimit(int count, int limit) {
  if (limit != null && count > limit) {
    throw FidlError('Found an object wth $count elements. Limited to $limit.',
        FidlErrorCode.fidlStringTooLong);
  }
}

void _throwIfCountMismatch(int count, int expectedCount) {
  if (count != expectedCount) {
    throw FidlError('Found an array of count $count. Expected $expectedCount.');
  }
}

void _throwIfNotZero(int value) {
  if (value != 0) {
    throw FidlError('Expected zero, got: $value');
  }
}

// Threshold at which _copy* functions switch from a for loop for copying to
// setRange().
// For small loops, constructing a typed view and calling setRange can take
// twice as long as directly iterating over the list.
// This threshold is chosen somewhat arbitrarily based on partial data
// and may not be optimal.
const _copySetRangeThreshold = 64;

void _copyInt8(ByteData data, Int8List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    for (int i = 0; i < count; ++i) {
      data.setInt8(offset + i, value[i]);
    }
  } else {
    data.buffer.asInt8List(offset).setRange(0, value.length, value);
  }
}

void _copyUint8(ByteData data, Uint8List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    for (int i = 0; i < count; ++i) {
      data.setUint8(offset + i, value[i]);
    }
  } else {
    data.buffer.asUint8List(offset).setRange(0, value.length, value);
  }
}

void _copyInt16(ByteData data, Int16List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 2;
    for (int i = 0; i < count; ++i) {
      data.setInt16(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asInt16List(offset).setRange(0, value.length, value);
  }
}

void _copyUint16(ByteData data, Uint16List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 2;
    for (int i = 0; i < count; ++i) {
      data.setUint16(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asUint16List(offset).setRange(0, value.length, value);
  }
}

void _copyInt32(ByteData data, Int32List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 4;
    for (int i = 0; i < count; ++i) {
      data.setInt32(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asInt32List(offset).setRange(0, value.length, value);
  }
}

void _copyUint32(ByteData data, Uint32List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 4;
    for (int i = 0; i < count; ++i) {
      data.setUint32(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asUint32List(offset).setRange(0, value.length, value);
  }
}

void _copyInt64(ByteData data, Int64List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 8;
    for (int i = 0; i < count; ++i) {
      data.setInt64(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asInt64List(offset).setRange(0, value.length, value);
  }
}

void _copyUint64(ByteData data, Uint64List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 8;
    for (int i = 0; i < count; ++i) {
      data.setUint64(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asUint64List(offset).setRange(0, value.length, value);
  }
}

void _copyFloat32(ByteData data, Float32List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 4;
    for (int i = 0; i < count; ++i) {
      data.setFloat32(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asFloat32List(offset).setRange(0, value.length, value);
  }
}

void _copyFloat64(ByteData data, Float64List value, int offset) {
  if (value.length < _copySetRangeThreshold) {
    final int count = value.length;
    const int stride = 8;
    for (int i = 0; i < count; ++i) {
      data.setFloat64(offset + i * stride, value[i], Endian.little);
    }
  } else {
    data.buffer.asFloat64List(offset).setRange(0, value.length, value);
  }
}

const int kAllocAbsent = 0;
const int kAllocPresent = 0xFFFFFFFFFFFFFFFF;
const int kHandleAbsent = 0;
const int kHandlePresent = 0xFFFFFFFF;

abstract class FidlType<T> {
  const FidlType({this.inlineSize});

  final int inlineSize;

  int encodingInlineSize(Encoder encoder) {
    return inlineSize;
  }

  int decodingInlineSize(Decoder decoder) {
    return inlineSize;
  }

  void encode(Encoder encoder, T value, int offset);
  T decode(Decoder decoder, int offset);

  void encodeArray(Encoder encoder, List<T> value, int offset) {
    final int count = value.length;
    final int stride = encodingInlineSize(encoder);
    for (int i = 0; i < count; ++i) {
      encode(encoder, value[i], offset + i * stride);
    }
  }

  List<T> decodeArray(Decoder decoder, int count, int offset) {
    final List<T> list = List<T>(count);
    for (int i = 0; i < count; ++i) {
      list[i] = decode(decoder, offset + i * decodingInlineSize(decoder));
    }
    return list;
  }
}

abstract class NullableFidlType<T> extends FidlType<T> {
  const NullableFidlType({inlineSize, this.nullable})
      : super(inlineSize: inlineSize);

  final bool nullable;
}

/// This encodes/decodes the UnknowRawData assuming it is in an envelope, i.e.
/// payload bytes followed directly by handles.
class UnknownRawDataType extends FidlType<UnknownRawData> {
  const UnknownRawDataType(this.numBytes, this.numHandles)
      : super(inlineSize: numBytes);

  final int numBytes;
  final int numHandles;

  @override
  void encode(Encoder encoder, UnknownRawData value, int offset) {
    _copyUint8(encoder.data, value.data, offset);
    for (int i = 0; i < value.handles.length; i++) {
      encoder.addHandle(value.handles[i]);
    }
  }

  @override
  UnknownRawData decode(Decoder decoder, int offset) {
    final Uint8List data = Uint8List(numBytes);
    for (var i = 0; i < numBytes; i++) {
      data[i] = decoder.decodeUint8(offset + i);
    }
    final handles = List<Handle>(numHandles);
    for (var i = 0; i < numHandles; i++) {
      handles[i] = decoder.claimHandle();
    }
    return UnknownRawData(data, handles);
  }
}

class BoolType extends FidlType<bool> {
  const BoolType() : super(inlineSize: 1);

  @override
  void encode(Encoder encoder, bool value, int offset) {
    encoder.encodeBool(value, offset);
  }

  @override
  bool decode(Decoder decoder, int offset) => decoder.decodeBool(offset);
}

class StatusType extends Int32Type {
  const StatusType();
}

class Int8Type extends FidlType<int> {
  const Int8Type() : super(inlineSize: 1);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeInt8(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeInt8(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyInt8(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asInt8List(offset, count);
  }
}

class Int16Type extends FidlType<int> {
  const Int16Type() : super(inlineSize: 2);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeInt16(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeInt16(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyInt16(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asInt16List(offset, count);
  }
}

class Int32Type extends FidlType<int> {
  const Int32Type() : super(inlineSize: 4);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeInt32(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeInt32(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyInt32(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asInt32List(offset, count);
  }
}

class Int64Type extends FidlType<int> {
  const Int64Type() : super(inlineSize: 8);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeInt64(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeInt64(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyInt64(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asInt64List(offset, count);
  }
}

class Uint8Type extends FidlType<int> {
  const Uint8Type() : super(inlineSize: 1);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeUint8(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeUint8(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyUint8(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asUint8List(offset, count);
  }
}

class Uint16Type extends FidlType<int> {
  const Uint16Type() : super(inlineSize: 2);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeUint16(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeUint16(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyUint16(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asUint16List(offset, count);
  }
}

class Uint32Type extends FidlType<int> {
  const Uint32Type() : super(inlineSize: 4);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeUint32(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeUint32(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyUint32(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asUint32List(offset, count);
  }
}

class Uint64Type extends FidlType<int> {
  const Uint64Type() : super(inlineSize: 8);

  @override
  void encode(Encoder encoder, int value, int offset) {
    encoder.encodeUint64(value, offset);
  }

  @override
  int decode(Decoder decoder, int offset) => decoder.decodeUint64(offset);

  @override
  void encodeArray(Encoder encoder, List<int> value, int offset) {
    _copyUint64(encoder.data, value, offset);
  }

  @override
  List<int> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asUint64List(offset, count);
  }
}

class Float32Type extends FidlType<double> {
  const Float32Type() : super(inlineSize: 4);

  @override
  void encode(Encoder encoder, double value, int offset) {
    encoder.encodeFloat32(value, offset);
  }

  @override
  double decode(Decoder decoder, int offset) => decoder.decodeFloat32(offset);

  @override
  void encodeArray(Encoder encoder, List<double> value, int offset) {
    _copyFloat32(encoder.data, value, offset);
  }

  @override
  List<double> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asFloat32List(offset, count);
  }
}

class Float64Type extends FidlType<double> {
  const Float64Type() : super(inlineSize: 8);

  @override
  void encode(Encoder encoder, double value, int offset) {
    encoder.encodeFloat64(value, offset);
  }

  @override
  double decode(Decoder decoder, int offset) => decoder.decodeFloat64(offset);

  @override
  void encodeArray(Encoder encoder, List<double> value, int offset) {
    _copyFloat64(encoder.data, value, offset);
  }

  @override
  List<double> decodeArray(Decoder decoder, int count, int offset) {
    return decoder.data.buffer.asFloat64List(offset, count);
  }
}

void _validateEncodedHandle(int encoded, bool nullable) {
  if (encoded == kHandleAbsent) {
    _throwIfNotNullable(nullable);
  } else if (encoded == kHandlePresent) {
    // Nothing to validate.
  } else {
    throw FidlError('Invalid handle encoding: $encoded.');
  }
}

void _encodeHandle(Encoder encoder, Handle value, int offset, bool nullable) {
  int encoded =
      (value != null && value.isValid) ? kHandlePresent : kHandleAbsent;
  _validateEncodedHandle(encoded, nullable);
  encoder.encodeUint32(encoded, offset);
  if (encoded == kHandlePresent) {
    encoder.addHandle(value);
  }
}

Handle _decodeHandle(Decoder decoder, int offset, bool nullable) {
  final int encoded = decoder.decodeUint32(offset);
  _validateEncodedHandle(encoded, nullable);
  return encoded == kHandlePresent ? decoder.claimHandle() : null;
}

// TODO(pascallouis): By having _HandleWrapper exported, we could DRY this code
// by simply having an AbstractHandleType<H extend HandleWrapper<H>> and having
// the encoding / decoding once, with the only specialization on a per-type
// basis being construction.
// Further, if each HandleWrapper were to offer a static ctor function to invoke
// their constrctors, could be called directly.
// We could also explore having a Handle be itself a subtype of HandleWrapper
// to further standardize handling of handles.

class HandleType extends NullableFidlType<Handle> {
  const HandleType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, Handle value, int offset) {
    _encodeHandle(encoder, value, offset, nullable);
  }

  @override
  Handle decode(Decoder decoder, int offset) =>
      _decodeHandle(decoder, offset, nullable);
}

class ChannelType extends NullableFidlType<Channel> {
  const ChannelType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, Channel value, int offset) {
    _encodeHandle(encoder, value?.handle, offset, nullable);
  }

  @override
  Channel decode(Decoder decoder, int offset) =>
      Channel(_decodeHandle(decoder, offset, nullable));
}

class EventPairType extends NullableFidlType<EventPair> {
  const EventPairType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, EventPair value, int offset) {
    _encodeHandle(encoder, value?.handle, offset, nullable);
  }

  @override
  EventPair decode(Decoder decoder, int offset) =>
      EventPair(_decodeHandle(decoder, offset, nullable));
}

class SocketType extends NullableFidlType<Socket> {
  const SocketType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, Socket value, int offset) {
    _encodeHandle(encoder, value?.handle, offset, nullable);
  }

  @override
  Socket decode(Decoder decoder, int offset) =>
      Socket(_decodeHandle(decoder, offset, nullable));
}

class VmoType extends NullableFidlType<Vmo> {
  const VmoType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, Vmo value, int offset) {
    _encodeHandle(encoder, value?.handle, offset, nullable);
  }

  @override
  Vmo decode(Decoder decoder, int offset) =>
      Vmo(_decodeHandle(decoder, offset, nullable));
}

class InterfaceHandleType<T> extends NullableFidlType<InterfaceHandle<T>> {
  const InterfaceHandleType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, InterfaceHandle<T> value, int offset) {
    _encodeHandle(encoder, value?.channel?.handle, offset, nullable);
  }

  @override
  InterfaceHandle<T> decode(Decoder decoder, int offset) {
    final Handle handle = _decodeHandle(decoder, offset, nullable);
    return InterfaceHandle<T>(handle.isValid ? Channel(handle) : null);
  }
}

class InterfaceRequestType<T> extends NullableFidlType<InterfaceRequest<T>> {
  const InterfaceRequestType({
    bool nullable,
  }) : super(inlineSize: 4, nullable: nullable);

  @override
  void encode(Encoder encoder, InterfaceRequest<T> value, int offset) {
    _encodeHandle(encoder, value?.channel?.handle, offset, nullable);
  }

  @override
  InterfaceRequest<T> decode(Decoder decoder, int offset) {
    final Handle handle = _decodeHandle(decoder, offset, nullable);
    return InterfaceRequest<T>(handle.isValid ? Channel(handle) : null);
  }
}

class StringType extends NullableFidlType<String> {
  const StringType({
    this.maybeElementCount,
    bool nullable,
  }) : super(inlineSize: 16, nullable: nullable);

  final int maybeElementCount;

  // See fidl_string_t.

  @override
  void encode(Encoder encoder, String value, int offset) {
    validate(value);
    if (value == null) {
      encoder
        ..encodeUint64(0, offset) // size
        ..encodeUint64(kAllocAbsent, offset + 8); // data
      return null;
    }
    final bytes = Utf8Encoder().convert(value);
    final int size = bytes.length;
    encoder
      ..encodeUint64(size, offset) // size
      ..encodeUint64(kAllocPresent, offset + 8); // data
    int childOffset = encoder.alloc(size);
    _copyUint8(encoder.data, bytes, childOffset);
  }

  @override
  String decode(Decoder decoder, int offset) {
    final int size = decoder.decodeUint64(offset);
    final int data = decoder.decodeUint64(offset + 8);
    validateEncoded(size, data);
    if (data == kAllocAbsent) {
      return null;
    }
    final Uint8List bytes =
        decoder.data.buffer.asUint8List(decoder.claimMemory(size), size);
    try {
      return const Utf8Decoder().convert(bytes, 0, size);
    } on FormatException {
      throw FidlError('Received a string with invalid UTF8: $bytes');
    }
  }

  void validate(String value) {
    if (value == null) {
      _throwIfNotNullable(nullable);
      return;
    }
    _throwIfExceedsLimit(value.length, maybeElementCount);
  }

  void validateEncoded(int size, int data) {
    if (data == kAllocAbsent) {
      _throwIfNotNullable(nullable);
      _throwIfNotZero(size);
    } else if (data == kAllocPresent) {
      _throwIfExceedsLimit(size, maybeElementCount);
    } else {
      throw FidlError('Invalid string encoding: $data.');
    }
  }
}

class PointerType<T> extends FidlType<T> {
  const PointerType({
    this.element,
  }) : super(inlineSize: 8);

  final FidlType element;

  @override
  void encode(Encoder encoder, T value, int offset) {
    if (value == null) {
      encoder.encodeUint64(kAllocAbsent, offset);
    } else {
      encoder.encodeUint64(kAllocPresent, offset);
      int childOffset = encoder.alloc(element.encodingInlineSize(encoder));
      element.encode(encoder, value, childOffset);
    }
  }

  @override
  T decode(Decoder decoder, int offset) {
    final int data = decoder.decodeUint64(offset);
    validateEncoded(data);
    if (data == kAllocAbsent) {
      return null;
    }
    return element.decode(
        decoder, decoder.claimMemory(element.decodingInlineSize(decoder)));
  }

  void validateEncoded(int encoded) {
    if (encoded != kAllocAbsent && encoded != kAllocPresent) {
      throw FidlError('Invalid pointer encoding: $encoded.');
    }
  }
}

class MemberType<T> extends FidlType<T> {
  const MemberType({
    this.type,
    this.offset,
  });

  final FidlType type;
  final int offset;

  @override
  void encode(Encoder encoder, T value, int base) {
    type.encode(encoder, value, base + offset);
  }

  @override
  T decode(Decoder decoder, int base) {
    return type.decode(decoder, base + offset);
  }
}

class StructType<T extends Struct> extends FidlType<T> {
  const StructType({
    int inlineSize,
    this.structDecode,
  }) : super(inlineSize: inlineSize);

  final StructDecode<T> structDecode;

  @override
  void encode(Encoder encoder, T value, int offset) {
    value.$encode(encoder, offset);
  }

  @override
  T decode(Decoder decoder, int offset) {
    return structDecode(decoder, offset);
  }
}

const int _kEnvelopeSize = 16;

void _encodeEnvelopePresent<T>(
    Encoder encoder, int offset, T field, FidlType<T> fieldType) {
  int numHandles = encoder.countHandles();
  final fieldOffset = encoder.alloc(fieldType.encodingInlineSize(encoder));
  fieldType.encode(encoder, field, fieldOffset);
  numHandles = encoder.countHandles() - numHandles;
  final numBytes = encoder.nextOffset() - fieldOffset;

  encoder
    ..encodeUint32(numBytes, offset)
    ..encodeUint32(numHandles, offset + 4)
    ..encodeUint64(kAllocPresent, offset + 8);
}

void _encodeEnvelopeAbsent(Encoder encoder, int offset) {
  encoder..encodeUint64(0, offset)..encodeUint64(kAllocAbsent, offset + 8);
}

class EnvelopeHeader {
  int numBytes;
  int numHandles;
  int fieldPresent;
  EnvelopeHeader(this.numBytes, this.numHandles, this.fieldPresent);
}

T _decodeEnvelope<T>(Decoder decoder, int offset, FidlType<T> fieldType,
    {bool isEmpty = false}) {
  final header = _decodeEnvelopeHeader(decoder, offset);
  return _decodeEnvelopeContent(decoder, header, fieldType, isEmpty);
}

EnvelopeHeader _decodeEnvelopeHeader(Decoder decoder, int offset) {
  return EnvelopeHeader(
    decoder.decodeUint32(offset),
    decoder.decodeUint32(offset + 4),
    decoder.decodeUint64(offset + 8),
  );
}

T _decodeEnvelopeContent<T>(Decoder decoder, EnvelopeHeader header,
    FidlType<T> fieldType, bool isEmpty) {
  switch (header.fieldPresent) {
    case kAllocPresent:
      if (isEmpty) throw FidlError('expected empty envelope');
      final fieldKnown = fieldType != null;
      if (fieldKnown) {
        final fieldOffset =
            decoder.claimMemory(fieldType.decodingInlineSize(decoder));
        final claimedHandles = decoder.countClaimedHandles();
        final field = fieldType.decode(decoder, fieldOffset);
        final numBytesConsumed = decoder.nextOffset() - fieldOffset;
        final numHandlesConsumed =
            decoder.countClaimedHandles() - claimedHandles;
        if (header.numBytes != numBytesConsumed)
          throw FidlError('field was mis-sized');
        if (header.numHandles != numHandlesConsumed)
          throw FidlError('handles were mis-sized');
        return field;
      }

      decoder.claimMemory(header.numBytes);
      for (int i = 0; i < header.numHandles; i++) {
        final handle = decoder.claimHandle();
        try {
          handle.close();
          // ignore: avoid_catches_without_on_clauses
        } catch (e) {
          // best effort
        }
      }
      return null;
      break;
    case kAllocAbsent:
      if (header.numBytes != 0)
        throw FidlError('absent envelope with non-zero bytes');
      if (header.numHandles != 0)
        throw FidlError('absent envelope with non-zero handles');
      return null;
    default:
      throw FidlError('Bad reference encoding');
  }
}

class TableType<T extends Table> extends FidlType<T> {
  const TableType({
    int inlineSize,
    this.members,
    this.ctor,
  }) : super(inlineSize: inlineSize);

  final List<FidlType> members;
  final TableFactory<T> ctor;

  @override
  void encode(Encoder encoder, T value, int offset) {
    // Determining max index
    int maxIndex = -1;
    for (int i = 0; i < members.length; i++) {
      final field = value.$field(i);
      if (field != null) {
        maxIndex = i;
      }
    }
    int maxOrdinal = maxIndex + 1;

    // Header.
    encoder
      ..encodeUint64(maxOrdinal, offset)
      ..encodeUint64(kAllocPresent, offset + 8);

    // Sizing
    int envelopeOffset = encoder.alloc(maxOrdinal * _kEnvelopeSize);

    // Envelopes, and fields.
    final unknownDataMap = value.$unknownData;
    final hasUnknownData = unknownDataMap != null;
    for (int i = 0; i <= maxIndex; i++) {
      var field = value.$field(i);
      var fieldType = members[i];
      if (hasUnknownData && fieldType == null) {
        final unknownData = unknownDataMap[i];
        if (unknownData != null) {
          field = unknownData;
          fieldType = UnknownRawDataType(
              unknownData.data.length, unknownData.handles.length);
        }
      }

      final fieldPresent = field != null;
      if (fieldPresent) {
        _encodeEnvelopePresent(encoder, envelopeOffset, field, fieldType);
      } else {
        _encodeEnvelopeAbsent(encoder, envelopeOffset);
      }
      envelopeOffset += _kEnvelopeSize;
    }
  }

  @override
  T decode(Decoder decoder, int offset) {
    // Header.
    final int maxOrdinal = decoder.decodeUint64(offset);
    final int data = decoder.decodeUint64(offset + 8);
    switch (data) {
      case kAllocPresent:
        break; // good
      case kAllocAbsent:
        throw FidlError('Unexpected null reference');
      default:
        throw FidlError('Bad reference encoding');
    }

    // Early exit on empty table.
    if (maxOrdinal == 0) {
      return ctor({});
    }

    // Offsets.
    int envelopeOffset = decoder.claimMemory(maxOrdinal * _kEnvelopeSize);

    // Envelopes, and fields.
    final Map<int, dynamic> argv = {};
    Map<int, UnknownRawData> unknownData;
    for (int ordinal = 1; ordinal <= maxOrdinal; ordinal++) {
      FidlType fieldType;
      if (ordinal <= members.length) {
        fieldType = members[ordinal - 1];
      } else {
        fieldType = null;
      }

      final header = _decodeEnvelopeHeader(decoder, envelopeOffset);
      final field = _decodeEnvelopeContent(
          decoder,
          header,
          fieldType ?? UnknownRawDataType(header.numBytes, header.numHandles),
          false);
      if (field != null) {
        if (fieldType != null) {
          argv[ordinal] = field;
        } else {
          unknownData ??= {};
          unknownData[ordinal] = field;
        }
      }
      envelopeOffset += _kEnvelopeSize;
    }

    return ctor(argv, unknownData);
  }
}

class XUnionType<T extends XUnion> extends NullableFidlType<T> {
  const XUnionType({
    this.members,
    this.ctor,
    bool nullable,
    this.flexible,
  }) : super(inlineSize: 24, nullable: nullable);

  final Map<int, FidlType> members;
  final XUnionFactory<T> ctor;
  final bool flexible;

  @override
  void encode(Encoder encoder, T value, int offset) {
    final int envelopeOffset = offset + 8;
    if (value == null) {
      if (!nullable) {
        _throwIfNotNullable(nullable);
      }
      encoder.encodeUint64(0, offset);
      _encodeEnvelopeAbsent(encoder, envelopeOffset);
    } else {
      final int ordinal = value.$ordinal;
      var fieldType = members[ordinal];
      if (fieldType == null && flexible) {
        UnknownRawData rawData = value.$data;
        fieldType =
            UnknownRawDataType(rawData.data.length, rawData.handles.length);
      }
      if (fieldType == null)
        throw FidlError('Bad xunion ordinal: $ordinal',
            FidlErrorCode.fidlStrictXUnionUnknownField);

      encoder.encodeUint64(ordinal, offset);
      _encodeEnvelopePresent(encoder, envelopeOffset, value.$data, fieldType);
    }
  }

  @override
  T decode(Decoder decoder, int offset) {
    final int envelopeOffset = offset + 8;
    final int ordinal = decoder.decodeUint64(offset);
    if (ordinal == 0) {
      if (!nullable) {
        throw FidlError('Zero xunion ordinal on non-nullable');
      }
      _decodeEnvelope(decoder, envelopeOffset, null, isEmpty: true);
      return null;
    } else {
      final header = _decodeEnvelopeHeader(decoder, envelopeOffset);
      var fieldType = members[ordinal];
      if (fieldType == null) {
        final unknownData = _decodeEnvelopeContent(decoder, header,
            UnknownRawDataType(header.numBytes, header.numHandles), false);
        if (unknownData == null) throw FidlError('Bad xunion: missing content');

        if (!flexible) {
          unknownData.closeHandles();
          throw FidlError('Bad xunion ordinal: $ordinal',
              FidlErrorCode.fidlStrictXUnionUnknownField);
        }
        return ctor(ordinal, unknownData);
      }
      final field = _decodeEnvelopeContent(decoder, header, fieldType, false);
      if (field == null) throw FidlError('Bad xunion: missing content');
      return ctor(ordinal, field);
    }
  }
}

class EnumType<T extends Enum> extends FidlType<T> {
  const EnumType({
    this.type,
    this.values,
    this.ctor,
  });

  final FidlType<int> type;
  // TODO(fxb/7644): Currently, types are generated as consts. This means that
  // we cannot use a Set, since there is no const Set. What we want to do
  // instead is to generate types as vars, avoid cycles which are problematic,
  // and once that's done, we can switch this to a Set.
  // TODO(fxb/8008): Use this set to determine whether this value is unknown.
  // (Alternative design would be to have a boolean field isUnknown.)
  final Map<int, bool> values;
  final EnumFactory<T> ctor;

  @override
  int get inlineSize => type.inlineSize;

  @override
  void encode(Encoder encoder, T value, int offset) {
    if (value == null) _throwIfNotNullable(false);
    type.encode(encoder, value.$value, offset);
  }

  @override
  T decode(Decoder decoder, int offset) {
    return ctor(type.decode(decoder, offset));
  }
}

class BitsType<T extends Bits> extends FidlType<T> {
  const BitsType({
    this.type,
    this.ctor,
  });

  final FidlType<int> type;
  final BitsFactory<T> ctor;

  @override
  int get inlineSize => type.inlineSize;

  @override
  void encode(Encoder encoder, T value, int offset) {
    type.encode(encoder, value.$value, offset);
  }

  @override
  T decode(Decoder decoder, int offset) {
    return ctor(type.decode(decoder, offset));
  }
}

class MethodType extends FidlType<Null> {
  const MethodType({
    this.request,
    this.response,
    this.name,
    this.requestInlineSize,
    this.responseInlineSize,
  });

  final List<MemberType> request;
  final List<MemberType> response;
  final String name;
  final int requestInlineSize;
  final int responseInlineSize;

  int encodingRequestInlineSize(Encoder encoder) {
    return requestInlineSize;
  }

  int encodingResponseInlineSize(Encoder encoder) {
    return responseInlineSize;
  }

  int decodeRequestInlineSize(Decoder decoder) {
    return requestInlineSize;
  }

  int decodeResponseInlineSize(Decoder decoder) {
    return responseInlineSize;
  }

  @override
  void encode(Encoder encoder, Null value, int offset) {
    throw FidlError('Cannot encode a method.');
  }

  @override
  Null decode(Decoder decoder, int offset) {
    throw FidlError('Cannot decode a method.');
  }
}

class VectorType<T extends List> extends NullableFidlType<T> {
  const VectorType({
    this.element,
    this.maybeElementCount,
    bool nullable,
  }) : super(inlineSize: 16, nullable: nullable);

  final FidlType element;
  final int maybeElementCount;

  @override
  void encode(Encoder encoder, T value, int offset) {
    validate(value);
    if (value == null) {
      encoder
        ..encodeUint64(0, offset) // count
        ..encodeUint64(kAllocAbsent, offset + 8); // data
    } else {
      final int count = value.length;
      encoder
        ..encodeUint64(count, offset) // count
        ..encodeUint64(kAllocPresent, offset + 8); // data
      int childOffset =
          encoder.alloc(count * element.encodingInlineSize(encoder));
      element.encodeArray(encoder, value, childOffset);
    }
  }

  @override
  T decode(Decoder decoder, int offset) {
    final int count = decoder.decodeUint64(offset);
    final int data = decoder.decodeUint64(offset + 8);
    validateEncoded(count, data);
    if (data == kAllocAbsent) {
      return null;
    }
    final int base =
        decoder.claimMemory(count * element.decodingInlineSize(decoder));
    return element.decodeArray(decoder, count, base);
  }

  void validate(T value) {
    if (value == null) {
      _throwIfNotNullable(nullable);
      return;
    }
    _throwIfExceedsLimit(value.length, maybeElementCount);
  }

  void validateEncoded(int count, int data) {
    if (data == kAllocAbsent) {
      _throwIfNotNullable(nullable);
      _throwIfNotZero(count);
    } else if (data == kAllocPresent) {
      _throwIfExceedsLimit(count, maybeElementCount);
    } else {
      throw FidlError('Invalid vector encoding: $data.');
    }
  }
}

class ArrayType<T extends List> extends FidlType<T> {
  const ArrayType({
    this.element,
    this.elementCount,
  });

  final FidlType element;
  final int elementCount;

  @override
  int get inlineSize => elementCount * element.inlineSize;

  @override
  void encode(Encoder encoder, T value, int offset) {
    validate(value);
    element.encodeArray(encoder, value, offset);
  }

  @override
  T decode(Decoder decoder, int offset) {
    return element.decodeArray(decoder, elementCount, offset);
  }

  void validate(T value) {
    _throwIfCountMismatch(value.length, elementCount);
  }
}
