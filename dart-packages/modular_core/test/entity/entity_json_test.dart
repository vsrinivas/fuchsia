// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/entity/entity.dart';
import 'package:modular_core/entity/schema.dart';

void main() {
  SchemaRegistry registry;

  setUp(() {
    registry = new SchemaRegistry();

    final Schema person = new Schema('person',
        [new Property.string('name'), new Property.dateTime('birthdate'),]);
    person.publish(registry);

    final Schema family = new Schema('family', [
      new Property('parents', 'person', isRepeated: true),
      new Property('children', 'person', isRepeated: true),
      new Property.string('pets', isRepeated: true),
      new Property.string('city'),
    ]);
    family.publish(registry);
  });

  test('serialize simple', () {
    final Entity empty = new Entity(['person'], registry: registry);
    expect(
        empty.toJsonWithoutMetadata(),
        equals({
          "schemas": ["person"],
          "values": {}
        }));

    final Entity ian = new Entity(['person'], registry: registry);
    ian['name'] = 'Ian McKellar';
    ian['birthdate'] = new DateTime.utc(1977, 10, 15);
    final dynamic jian = ian.toJsonWithoutMetadata();
    expect(
        jian,
        equals({
          "schemas": ["person"],
          "values": {
            "person#name": "Ian McKellar",
            "person#birthdate": "1977-10-15T00:00:00.000Z",
          }
        }));
    expect(
        jian,
        equals(new Entity.fromJsonWithoutMetadata(jian, registry: registry)
            .toJsonWithoutMetadata()));
  });

  test('serialize complex', () {
    final Entity empty = new Entity(['family'], registry: registry);
    expect(
        empty.toJsonWithoutMetadata(),
        equals({
          "schemas": ["family"],
          "values": {}
        }));

    final Entity ian = new Entity(['person'], registry: registry);
    ian['name'] = 'Ian McKellar';
    ian['birthdate'] = new DateTime.utc(1977, 10, 15);

    final Entity sharon = new Entity(['person'], registry: registry);
    sharon['name'] = 'Sharon McKellar';
    sharon['birthdate'] = new DateTime.utc(1975, 12, 20);

    final Entity mckellars = new Entity(['family'], registry: registry);
    mckellars['parents'] = [ian, sharon];
    mckellars['pets'] = ["Bella"];

    expect(
        mckellars.toJsonWithoutMetadata(),
        equals({
          'schemas': ['family'],
          'values': {
            'family#parents': [
              {
                'person#name': 'Ian McKellar',
                'person#birthdate': '1977-10-15T00:00:00.000Z',
              },
              {
                'person#name': 'Sharon McKellar',
                'person#birthdate': '1975-12-20T00:00:00.000Z',
              },
            ],
            'family#pets': ['Bella']
          }
        }));

    // Then we had twins but our cat died.
    mckellars['children'].length = 2;
    mckellars['children'][0] = new Entity(['person'], registry: registry);
    mckellars['children'][0]['name'] = 'Lillian McKellar';
    mckellars['children'][0]['birthdate'] = new DateTime.utc(2015, 7, 17);
    mckellars['children'][1] = new Entity(['person'], registry: registry);
    mckellars['children'][1]['name'] = 'Matilda McKellar';
    mckellars['children'][1]['birthdate'] =
        mckellars['children'][0]['birthdate'];
    mckellars['pets'].length = 0;

    expect(
        mckellars.toJsonWithoutMetadata(),
        equals({
          'schemas': ['family'],
          'values': {
            'family#parents': [
              {
                'person#name': 'Ian McKellar',
                'person#birthdate': '1977-10-15T00:00:00.000Z'
              },
              {
                'person#name': 'Sharon McKellar',
                'person#birthdate': '1975-12-20T00:00:00.000Z'
              }
            ],
            'family#children': [
              {
                'person#name': 'Lillian McKellar',
                'person#birthdate': '2015-07-17T00:00:00.000Z'
              },
              {
                'person#name': 'Matilda McKellar',
                'person#birthdate': '2015-07-17T00:00:00.000Z'
              }
            ]
          }
        }));
  });

  test('serialize metadata', () {
    const String creationTimeMetadataLabel = "creationTime";
    const String modifiedTimeMetadataLabel = "modifiedTime";

    final Entity empty = new Entity(['person'], registry: registry);
    expect(
        empty.toJsonWithoutMetadata(),
        equals({
          "schemas": ["person"],
          "values": {}
        }));

    final Entity ian = new Entity(['person'], registry: registry);
    ian['name'] = 'Ian McKellar';
    ian['birthdate'] = new DateTime.utc(1977, 10, 15);
    final dynamic jian = ian.toJson();
    expect(
        jian,
        equals({
          "schemas": ["person"],
          "metadata": {
            creationTimeMetadataLabel: ian.creationTime,
            modifiedTimeMetadataLabel: ian.modifiedTime
          },
          "values": {
            "person#name": "Ian McKellar",
            "person#birthdate": "1977-10-15T00:00:00.000Z",
          }
        }));
    expect(
        jian, equals(new Entity.fromJson(jian, registry: registry).toJson()));
  });
}
