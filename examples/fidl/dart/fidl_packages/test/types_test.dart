// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import]
import 'package:fidl_fuchsia_examples/fidl_async.dart' as fidl_examples;
// [END import]
import 'package:test/test.dart';

void main() {
  // [START bits]
  test('bits', () {
    expect(fidl_examples.FileMode.$none.$value, equals(0));
    expect(fidl_examples.FileMode.read.$value, equals(1));
    final readWrite =
        fidl_examples.FileMode.read | fidl_examples.FileMode.write;
    expect(readWrite.toString(), 'FileMode.read | FileMode.write');
  });
  // [END bits]

  // [START enums]
  test('enums', () {
    final museum = fidl_examples.LocationType.museum;
    expect(fidl_examples.LocationType(1), equals(museum));
    expect(fidl_examples.LocationType.$valueOf('museum'), equals(museum));
    expect(museum.toString(), 'LocationType.museum');
  });
  // [END enums]

  // [START structs]
  test('structs', () {
    final withDefaultName = fidl_examples.Color(id: 0);
    expect(withDefaultName.name, equals('red'));
    expect(withDefaultName.$fields, equals([0, 'red']));

    final blue = fidl_examples.Color(id: 1, name: 'blue');
    expect(blue == withDefaultName, equals(false));
    expect(blue.toString(), 'Color(id: 1, name: blue)');

    final deepBlue = fidl_examples.Color.clone(blue, name: 'deep blue');
    expect(deepBlue.id, equals(1));
    expect(blue == deepBlue, equals(false));
  });
  // [END structs]

  // [START unions]
  test('unions', () {
    final intVal = fidl_examples.JsonValue.withIntValue(1);
    expect(intVal.$tag, equals(fidl_examples.JsonValueTag.intValue));
    expect(intVal.intValue, equals(1));
    expect(
        intVal == fidl_examples.JsonValue.withStringValue('1'), equals(false));

    expect(intVal.toString(), 'JsonValue.intValue(1)');
  });
  // [END unions]

  // [START tables]
  test('tables', () {
    final user = fidl_examples.User();
    expect(user.age, equals(null));
    expect(user.name, equals(null));

    final fuchsia = fidl_examples.User(age: 100, name: 'fuchsia');
    expect(fuchsia.$fields, equals({2: 100, 3: 'fuchsia'}));
    expect(user == fuchsia, equals(false));
  });
  // [END tables]
}
