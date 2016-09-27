// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/entity/schema.dart';

void main() {
  test('JSON', () {
    Schema schema = new Schema('myType', [
      new Property.int('prop1'),
      new Property.dateTime('prop2', isRepeated: true),
      new Property('prop3', 'myOtherType')
    ]);

    final String jsonString = schema.toJsonString();
    expect(
        jsonString,
        equals('{"type":"myType","properties":['
            '{"name":"prop1","type":"int","isRepeated":false},'
            '{"name":"prop2","type":"datetime","isRepeated":true},'
            '{"name":"prop3","type":"myOtherType","isRepeated":false}]}'));
    Schema other = new Schema.fromJsonString(jsonString);
    expect(other.type, equals(schema.type));
    expect(other.properties.length, equals(schema.properties.length));
    expect(other.property('prop1').type, equals('int'));
    expect(other.property('prop1').isRepeated, equals(false));
    expect(other.property('prop2').type, equals('datetime'));
    expect(other.property('prop2').isRepeated, equals(true));
    expect(other.property('prop3').type, equals('myOtherType'));
    expect(other.property('prop3').isRepeated, equals(false));
  });

  test('Long names', () {
    Schema schema = new Schema('myType', []);
    expect(schema.propertyLongName('name'), equals('myType#name'));
  });

  test('Registry aliases', () {
    Schema schema = new Schema('myType', []);
    SchemaRegistry r = new SchemaRegistry();

    r.add(schema, ['myAlias']);

    expect(r.get('myType'), equals(schema));
    expect(r.get('myAlias'), equals(schema));

    // Add another schema with the same alias and expect an error.
    Schema otherSchema = new Schema('otherType', []);
    expect(() => r.add(otherSchema, ['myAlias']), throws);
  });
}
