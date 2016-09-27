// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Serialization bindings for built-in representation types.

import 'dart:convert';
import 'dart:typed_data';

import 'representation_types.dart';

typedef Uint8List Writer(final dynamic value);

class BuiltinInt {
  static const String label =
      "https://github.com/domokit/modular/wiki/representation#int";
  static final Uri uri = Uri.parse(label);
  static int read(Uint8List data) => data.buffer.asByteData().getInt32(0);
  static Uint8List write(final int value) =>
      (new ByteData(4)..setInt32(0, value)).buffer.asUint8List();
}

class BuiltinString {
  static const String label =
      "https://github.com/domokit/modular/wiki/representation#string";
  static final Uri uri = Uri.parse(label);
  static String read(final Uint8List data) => UTF8.decode(data);
  static Uint8List write(final String value) => UTF8.encode(value);
}

class BuiltinDateTime {
  static const String label =
      "https://github.com/domokit/modular/wiki/representation#date-time";
  static final Uri uri = Uri.parse(label);
  static DateTime read(final Uint8List data) =>
      DateTime.parse(UTF8.decode(data));
  static Uint8List write(final DateTime value) =>
      UTF8.encode(value.toIso8601String());
}

class BuiltinIntBindings implements RepresentationBindings<int> {
  @override
  final String label = BuiltinInt.label;

  const BuiltinIntBindings();

  @override
  int decode(Uint8List data) => BuiltinInt.read(data);

  @override
  Uint8List encode(int value) => BuiltinInt.write(value);
}

class BuiltinDateTimeBindings implements RepresentationBindings<DateTime> {
  @override
  final String label = BuiltinDateTime.label;

  const BuiltinDateTimeBindings();

  @override
  DateTime decode(Uint8List data) => BuiltinDateTime.read(data);

  @override
  Uint8List encode(DateTime value) => BuiltinDateTime.write(value);
}

class BuiltinStringBindings implements RepresentationBindings<String> {
  @override
  final String label = BuiltinString.label;

  const BuiltinStringBindings();

  @override
  String decode(Uint8List data) => BuiltinString.read(data);

  @override
  Uint8List encode(String value) => BuiltinString.write(value);
}

void registerBuiltinTypes(final RepresentationBindingsRegistry registry) {
  registry.register(DateTime, const BuiltinDateTimeBindings());
  registry.register(int, const BuiltinIntBindings());
  registry.register(String, const BuiltinStringBindings());
}
