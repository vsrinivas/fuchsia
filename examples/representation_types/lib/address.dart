// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular/builtin_types.dart';
import 'dart:typed_data';
import 'package:modular/representation_types.dart';

/// A street address string
class Address {
  final String address;
  const Address(this.address);

  @override
  String toString() => address;
  @override
  bool operator ==(Object other) =>
      (other is Address) && (toString() == other.toString());
  @override
  int get hashCode => address.hashCode;
  // Support JSON serialization.
  Object toJson() => address;
}

class AddressBindings implements RepresentationBindings<Address> {
  const AddressBindings();

  @override
  final String label =
      'https://github.com/domokit/modular/wiki/representation#address';

  @override
  Address decode(Uint8List data) {
    return new Address(BuiltinString.read(data));
  }

  @override
  Uint8List encode(Address value) {
    return BuiltinString.write(value.address);
  }
}
