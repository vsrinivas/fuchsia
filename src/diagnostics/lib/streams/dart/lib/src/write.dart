// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert' show utf8;
import 'dart:typed_data';

import 'package:fidl_fuchsia_diagnostics_stream/fidl_async.dart';

import 'internal/header.dart';

/// Type for a log record.
const int tracingFormatLogRecordType = 9;

/// Type for a signed 64 bit integer argument.
const int tracingFormatI64Type = 3;

/// Type for an unsigned 64 bit integer argument.
const int tracingFormatU64Type = 4;

/// Type for a 64 bit floating point argument.
const int tracingFormatF64Type = 5;

/// Type for a string argument.
const int tracingFormatStringType = 6;

/// Bit that indicates a referenced string is written inline.
const int _inlineStrRef = 0x8000;

/// Write the given record to the given buffer. The record is written as a
/// [log record] as specified in the Fuchsia Trace Format, with two exceptions:
///  * Process and thread koids are omitted.
///  * The message payload is replaced by an arguments payload.
/// Returns the number of bytes used in the buffer.
///
/// [log record]: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#log-record
int writeRecord(ByteData buffer, Record record) {
  final header = Header()
    ..setType(tracingFormatLogRecordType)
    ..setSeverity(record.severity.$value);

  var curOffset = 16;
  for (Argument argument in record.arguments) {
    curOffset = writeArgument(buffer, curOffset, argument);
  }
  header.setSize(curOffset);

  buffer
    ..setUint64(0, header.rawBits, Endian.little)
    ..setInt64(8, record.timestamp, Endian.little);
  return curOffset;
}

/// Write the given argument to the buffer, starting at the given offset. The
/// argument is written as specified in the [Fuchsia Trace Format]. Returns the
/// offset for any additional entries.
///
/// [Fuchsia Trace Format]: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#arguments
int writeArgument(ByteData buffer, int offset, Argument argument) {
  int startOffset = offset;
  final header = Header();

  final argNameWriteResult =
      writeString(buffer, startOffset + 8, argument.name);
  header.setNameRef(argNameWriteResult.strRef);

  var curOffset = argNameWriteResult.nextOffset;
  switch (argument.value.$tag) {
    case ValueTag.signedInt:
      header.setType(tracingFormatI64Type);
      buffer.setInt64(curOffset, argument.value.signedInt, Endian.little);
      curOffset += 8;
      break;
    case ValueTag.unsignedInt:
      header.setType(tracingFormatU64Type);
      buffer.setUint64(curOffset, argument.value.unsignedInt, Endian.little);
      curOffset += 8;
      break;
    case ValueTag.floating:
      header.setType(tracingFormatF64Type);
      buffer.setFloat64(curOffset, argument.value.floating, Endian.little);
      curOffset += 8;
      break;
    case ValueTag.text:
      header.setType(tracingFormatStringType);
      final textWriteRes = writeString(buffer, curOffset, argument.value.text);
      header.setValue(textWriteRes.strRef);
      curOffset = textWriteRes.nextOffset;
      break;
    default:
      throw ArgumentError('Unsupported value type');
  }
  header.setSize(curOffset - startOffset);
  buffer.setUint64(startOffset, header.rawBits, Endian.little);

  return curOffset;
}

class _WriteStringResult {
  /// Reference to the written string.
  final int strRef;

  /// Index the next entry should be written to.
  final int nextOffset;

  _WriteStringResult({this.strRef, this.nextOffset});
}

/// Writes the given string to the buffer and pads to 8 bytes.  Returns an
/// [string reference] for the written string and offset for
/// the next entry in the buffer (including any padding).
///
/// [string reference]: https://fuchsia.dev/fuchsia-src/development/logs/encodings#primitives
_WriteStringResult writeString(ByteData buffer, int offset, String str) {
  const align = 8;
  if (offset % align != 0) {
    throw ArgumentError('Offset is not 8-byte aligned');
  }

  var curOffset = offset;
  for (int char in utf8.encode(str)) {
    buffer.setUint8(curOffset, char);
    curOffset++;
  }
  final strLen = curOffset - offset;
  final strRef = strLen > 0 ? strLen | _inlineStrRef : 0;

  // zero out padding
  while (curOffset % align != 0) {
    buffer.setUint8(curOffset, 0);
    curOffset++;
  }

  return _WriteStringResult(strRef: strRef, nextOffset: curOffset);
}
