// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:modular/representation_types.dart';

/// Partial implementation of https://schema.org/Person.

/// Dart representation that modules can use.
class Person {
  String name;
  String email;
  String avatarUrl;

  @override
  String toString() => "$name <$email>";
}

/// Serialization bindings. This is implemented by hand, as `json_object`
/// requires mirrors. We should switch to some library once we find one that
/// does not.
class PersonBindings implements RepresentationBindings<Person> {
  @override
  final String label = 'https://schema.org/Person';

  const PersonBindings();

  @override
  Person decode(Uint8List data) {
    final String json = UTF8.decode(data);
    dynamic decoded = JSON.decode(json);
    return new Person()
      ..name = decoded['name']
      ..email = decoded['email']
      ..avatarUrl = decoded['avatarUrl'];
  }

  @override
  Uint8List encode(Person value) {
    final dynamic map = {};
    map['name'] = value.name;
    map['email'] = value.email;
    map['avatarUrl'] = value.avatarUrl;
    return UTF8.encode(JSON.encode(map));
  }
}
