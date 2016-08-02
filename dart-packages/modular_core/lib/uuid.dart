// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:uuid/uuid.dart' as uuid_gen;

import 'util/base64_converter.dart';

/// [Uuid] represents a standard 128-bit unique identifier.
class Uuid {
  final List<int> data;
  static final uuid_gen.Uuid _gen = new uuid_gen.Uuid();

  Uuid(this.data) {
    assert(data != null);
    assert(data.length == 16);
  }

  /// Generates a new v4 random [Uuid].
  Uuid.random() : data = new List<int>(16) {
    _gen.v4(buffer: this.data);
  }

  /// Generates a new [Uuid] whose values are all zero.
  Uuid.zero() : data = new List<int>.filled(16, 0);

  /// Generates a new [Uuid] from the given base64 encoded string. If the given
  /// String is null,  null will be returned.
  static Uuid fromBase64(String base64Uuid) {
    if (base64Uuid == null) {
      return null;
    }
    return new Uuid(Base64.decodeToList(base64Uuid));
  }

  /// Converts this [Uuid] to base64 string.
  String toBase64() {
    return Base64.encodeList(data);
  }

  /// Converts this [Uuid] to a [Uint8List].
  Uint8List toUint8List() => new Uint8List.fromList(data);

  @override // Object
  bool operator ==(Object other) {
    return (other is Uuid) &&
        const ListEquality<int>().equals(data, other.data);
  }

  @override // Object
  int get hashCode {
    return const ListEquality<int>().hash(data);
  }

  @override // Object
  String toString() {
    return _gen.unparse(this.data);
  }
}
