// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular/builtin_types.dart';
import 'package:modular/representation_types.dart';
import 'package:test/test.dart';

void main() {
  group('Builtin representation type bindings', () {
    test('Built-in date time', () async {
      final RepresentationBindingsRegistry bindingsRegistry =
          new RepresentationBindingsRegistry();
      bindingsRegistry.register(DateTime, const BuiltinDateTimeBindings());
      DateTime originalValue;
      DateTime readValue;

      originalValue = new DateTime(1989, DateTime.NOVEMBER, 9);
      readValue = bindingsRegistry.read(bindingsRegistry.write(originalValue));
      expect(readValue, equals(originalValue));
    });

    test('Built-in int', () async {
      final RepresentationBindingsRegistry bindingsRegistry =
          new RepresentationBindingsRegistry();
      bindingsRegistry.register(int, const BuiltinIntBindings());
      int originalValue;
      int readValue;

      originalValue = 42;
      readValue = bindingsRegistry.read(bindingsRegistry.write(originalValue));
      expect(readValue, equals(originalValue));
    });

    test('Built-in string', () async {
      final RepresentationBindingsRegistry bindingsRegistry =
          new RepresentationBindingsRegistry();
      bindingsRegistry.register(String, const BuiltinStringBindings());
      String originalValue;
      String readValue;

      originalValue = "Pizza";
      readValue = bindingsRegistry.read(bindingsRegistry.write(originalValue));
      expect(readValue, equals(originalValue));
    });
  });
}
