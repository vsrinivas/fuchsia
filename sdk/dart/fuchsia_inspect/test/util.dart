// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

/// Returns the ascii code of this character.
int ascii(String char) {
  if (char.length != 1) {
    throw ArgumentError('char must be 1 character long.');
  }
  var code = char.codeUnitAt(0);
  if (code > 127) {
    throw ArgumentError("char wasn't ascii (code $code)");
  }
  return code;
}

/// returns the hex char corresponding to a 0..15 value.
String hexChar(int value) {
  if (value < 0 || value > 15) {
    throw ArgumentError('Bad value $value');
  }
  return value.toRadixString(16);
}

/// Compares contents, starting at [offset], with the hex values in [spec].
///
/// Valid chars in [spec] are:
///   ' ' (ignored completely)
///   _ x X (skips 4 bits)
///   0..9 a..f A..F (hex value of 4 bits)
///
/// [spec] is little-endian, which makes integer values look weird. If you
/// write 0x234 into memory, it'll be matched by '34 02' (or by 'x4_2')
void compare(FakeVmoHolder vmo, int offset, String spec) {
  int nybble = offset * 2;
  for (int i = 0; i < spec.length; i++) {
    int rune = spec.codeUnitAt(i);
    if (rune == ascii(' ')) {
      continue;
    }
    if (rune == ascii('_') || rune == ascii('x') || rune == ascii('X')) {
      nybble++;
      continue;
    }
    int value;
    if (rune >= ascii('0') && rune <= ascii('9')) {
      value = rune - ascii('0');
    } else if (rune >= ascii('a') && rune <= ascii('f')) {
      value = rune - ascii('a') + 10;
    } else if (rune >= ascii('A') && rune <= ascii('F')) {
      value = rune - ascii('A') + 10;
    } else {
      throw ArgumentError('Illegal char "${String.fromCharCode(rune)}"');
    }
    int byte = nybble ~/ 2;
    int dataAtByte = vmo.bytes.getUint8(byte);
    int dataAtNybble = (nybble & 1 == 0) ? dataAtByte >> 4 : dataAtByte & 0xf;
    if (dataAtNybble != value) {
      expect(dataAtNybble, value,
          reason:
              'byte[0x${byte.toRadixString(16)}] = ${dataAtByte.toRadixString(16)}. '
              'Nybble $nybble [0x${nybble.toRadixString(16)}] was ${dataAtNybble.toRadixString(16)} '
              'but expected ${value.toRadixString(16)}.\n${dumpBlocks(vmo)}');
    }
    nybble++;
  }
}

/// Writes block contents in hexadecimal, nicely formatted, to stdout.
///
/// A dump should be printed when a test fails, so that the log can be
/// inspected on errors.
String dumpBlocks(FakeVmoHolder vmo, {int startIndex = 0, int howMany32 = -1}) {
  int lastIndex = (howMany32 == -1)
      ? (vmo.bytes.lengthInBytes >> 4) - 1
      : startIndex + howMany32 - 1;
  var buffer = StringBuffer()
    ..writeln(
        'Dumping blocks from $startIndex through $lastIndex for debugging.')
    ..writeln('  ,----------- byte offset')
    ..writeln('  |   ,------- index')
    ..writeln('  |   |   ,--- order (0:16 bytes; 1:32 bytes; etc.)')
    ..writeln(
        '  |   |   |   ,- type (0: free; 1:reserved; 2:header; 3:object value; 8:extent, 9:name; 10:tombstone)')
    ..writeln('  v   v   v   v');
  for (int index = startIndex; index <= lastIndex;) {
    String lowNybble(int offset) => hexChar(vmo.bytes.getUint8(offset) & 15);
    String highNybble(int offset) => hexChar(vmo.bytes.getUint8(offset) >> 4);
    buffer.write(
        '${(index * 16).toRadixString(16).padLeft(3, '0')}[${index.toString().padLeft(3, ' ')}]: ');
    for (int byte = 0; byte < 8; byte++) {
      buffer
        ..write('${lowNybble(index * 16 + byte)} ')
        ..write('${highNybble(index * 16 + byte)} ');
    }
    int order = vmo.bytes.getUint8(index * 16) & 0xf;
    int numWords = 1 << (order + 1);
    String byteToHex(int offset) =>
        vmo.bytes.getUint8(offset).toRadixString(16).padLeft(2, '0');
    for (int word = 1; word < numWords; word++) {
      buffer.write('  ');
      for (int byte = 0; byte < 8; byte++) {
        buffer.write('${byteToHex(index * 16 + word * 8 + byte)} ');
      }
    }
    index += 1 << order;
    buffer.writeln('');
  }
  return buffer.toString();
}

/// A matcher that matches ByteData properties as unit8 lists.
Matcher equalsByteData(ByteData data) => _EqualsByteData(data);

class _EqualsByteData extends Matcher {
  final ByteData _other;

  const _EqualsByteData(this._other);

  @override
  bool matches(dynamic item, _) {
    if (item is! ByteData) {
      return false;
    }

    bool Function(List<dynamic>, List<dynamic>) listEquals =
        ListEquality().equals;

    ByteData byteData = item;
    return listEquals(
        byteData.buffer.asUint8List(), _other.buffer.asUint8List());
  }

  @override
  Description describe(Description description) =>
      description.add('buffer as uint8 list: ${_other.buffer.asUint8List()}');

  @override
  Description describeMismatch(
      dynamic item, Description mismatchDescription, _, __) {
    if (item is! ByteData) {
      return mismatchDescription.add('$item is not of type ByteData');
    }

    ByteData byteData = item;
    return mismatchDescription
        .replace('buffer as uint8 list: ${byteData.buffer.asUint8List()}');
  }
}
