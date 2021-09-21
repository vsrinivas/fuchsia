// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'bitfield64.dart';

/// A header in the tracing format. Expected to precede every Record and Argument.
///
/// The tracing format specifies [Record headers] and [Argument headers] as distinct types, but
/// their layouts are the same in practice, so we represent both bitfields using the same
/// struct.
///
/// [Record headers]: https://fuchsia.dev/fuchsia-src/development/tracing/trace-format#record_header
/// [Argument headers]: https://fuchsia.dev/fuchsia-src/development/tracing/trace-format#argument_header
class Header {
  final Bitfield64 _bits;

  static final _typeRange = BitRange(0, 3);
  static final _sizeRange = BitRange(4, 15);
  static final _nameRange = BitRange(16, 31);
  static final _valueRange = BitRange(32, 47);
  static final _severityRange = BitRange(56, 63);

  /// Create a new header, optionally initialized with a known header value.
  Header([int value = 0]) : _bits = Bitfield64(value);

  /// Set the type field.
  void setType(int rawType) => _bits.write(_typeRange, rawType);

  /// Raw type of the record.
  int get type => _bits.read(_typeRange);

  /// Set size, given in number of bytes.
  void setSize(int size) {
    if (size % 8 != 0) {
      throw ArgumentError('Size must be a multiple of 8.');
    }
    var sizeWords = size ~/ 8;
    _bits.write(_sizeRange, sizeWords);
  }

  /// Size of record in bytes.
  int get size => _bits.read(_sizeRange) * 8;

  /// Set string ref for associated name.
  void setNameRef(int nameRef) => _bits.write(_nameRange, nameRef);

  /// String ref for associated name.
  int get nameRef => _bits.read(_nameRange);

  /// Set any record specific value data.
  void setValue(int value) => _bits.write(_valueRange, value);

  /// Record specific value data.
  int get value => _bits.read(_valueRange);

  /// Set the severitu of the record.
  void setSeverity(int value) => _bits.write(_severityRange, value);

  /// Severity of the record.
  int get severity => _bits.read(_severityRange);

  /// Raw bits of the header.
  int get rawBits => _bits.value;
}
