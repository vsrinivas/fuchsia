// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: implementation_imports

import 'dart:convert' show Utf8Decoder;
import 'dart:typed_data';

import 'package:fidl_fuchsia_diagnostics/fidl_async.dart';
import 'package:fidl_fuchsia_diagnostics_stream/fidl_async.dart';
import 'package:fuchsia_diagnostic_streams/src/internal/header.dart';
import 'package:fuchsia_diagnostic_streams/src/write.dart';
import 'package:test/test.dart';

/// assert that buffer contains the serialized record.
void assertRecord(Record record, ByteData buffer) {
  var header = Header(buffer.getUint64(0, Endian.little));
  var timestamp = buffer.getInt64(8, Endian.little);

  expect(header.size, equals(buffer.lengthInBytes));
  expect(header.type, equals(9)); // log type

  List<Argument> arguments = [];
  var offset = 16;
  while (offset < buffer.lengthInBytes) {
    var parseResult = parseArgument(buffer, offset);
    arguments.add(parseResult.argument);
    offset = parseResult.nextOffset;
  }

  var foundRecord = Record(
      timestamp: timestamp,
      severity: Severity(header.severity),
      arguments: arguments);
  expect(foundRecord, equals(record));
}

class _ArgParseResult {
  final Argument argument;
  final int nextOffset;

  _ArgParseResult({this.argument, this.nextOffset});
}

/// parses an argument starting at the given offset.  Returns the found
/// argument and offset of the next entry.
_ArgParseResult parseArgument(ByteData buffer, int offset) {
  final header = Header(buffer.getUint64(offset, Endian.little));
  // Assume inline string reference for argument name. Last 15 bits are length.
  final nameLen = header.nameRef & 0x7fff;
  final padLen = (8 - (nameLen % 8)) % 8;
  final name =
      Utf8Decoder().convert(buffer.buffer.asInt8List(offset + 8, nameLen));

  final valuePos = offset + 8 + nameLen + padLen;
  Value value;
  var curOffset = valuePos;
  switch (header.type) {
    case tracingFormatI64Type:
      value = Value.withSignedInt(buffer.getInt64(valuePos, Endian.little));
      curOffset += 8;
      break;
    case tracingFormatU64Type: // unsigned int
      value = Value.withUnsignedInt(buffer.getUint64(valuePos, Endian.little));
      curOffset += 8;
      break;
    case tracingFormatF64Type: // floating point
      value = Value.withFloating(buffer.getFloat64(valuePos, Endian.little));
      curOffset += 8;
      break;
    case tracingFormatStringType: // string
      // Assume inline string reference for value.  Last 15 bits are length.
      final valLen = header.value & 0x7fff;
      final padLen = (8 - (valLen % 8)) % 8;
      final str =
          Utf8Decoder().convert(buffer.buffer.asInt8List(valuePos, valLen));
      value = Value.withText(str);
      curOffset += valLen + padLen;
      break;
    default:
      fail('Unrecognized header type ${header.type}');
  }

  expect(header.size, equals(curOffset - offset));
  return _ArgParseResult(
      argument: Argument(name: name, value: value), nextOffset: curOffset);
}

void testWriteRecord(Record record) {
  final bytes = ByteData(1024);
  final recordLen = writeRecord(bytes, record);
  assertRecord(record, bytes.buffer.asByteData(0, recordLen));
  expect(recordLen % 8, equals(0));
}

void testWriteArgument(Argument argument) {
  final bytes = ByteData(1024);
  final argLen = writeArgument(bytes, 0, argument);
  final argResult = parseArgument(bytes, 0);
  expect(argResult.argument, equals(argument));
  expect(argResult.nextOffset, equals(argLen));
  expect(argLen % 8, equals(0));
}

void main() {
  const int _testTimestamp = 23;
  group('write record tests', () {
    test('no arguments', () {
      testWriteRecord(Record(
          timestamp: _testTimestamp, severity: Severity.warn, arguments: []));
    });

    test('single argument', () {
      testWriteRecord(Record(
          timestamp: _testTimestamp,
          severity: Severity.info,
          arguments: [
            Argument(name: 'int-arg', value: Value.withSignedInt(2902))
          ]));
    });

    test('multiple arguments', () {
      final arguments = [
        Argument(name: '', value: Value.withText('')),
        Argument(name: 'arg-1', value: Value.withSignedInt(23)),
        Argument(name: 'arg-2', value: Value.withUnsignedInt(10)),
        Argument(name: 'arg-3', value: Value.withFloating(0.1)),
        Argument(name: 'arg-4', value: Value.withText('text'))
      ];
      testWriteRecord(Record(
          timestamp: _testTimestamp,
          severity: Severity.error,
          arguments: arguments));
    });
  });

  group('write argument tests', () {
    test('signed int', () {
      testWriteArgument(
          Argument(name: 'signed-arg', value: Value.withSignedInt(12)));
      testWriteArgument(
          Argument(name: 'signed-arg', value: Value.withSignedInt(-12)));
    });

    test('unsigned int', () {
      testWriteArgument(
          Argument(name: 'unsigned-arg', value: Value.withUnsignedInt(999)));
    });

    test('floating point', () {
      testWriteArgument(
          Argument(name: 'floating-arg', value: Value.withFloating(140.15)));
    });

    test('text', () {
      testWriteArgument(
          Argument(name: 'text-arg', value: Value.withText('arg-value')));
      testWriteArgument(Argument(name: 'text-arg', value: Value.withText('')));
    });
  });
}
