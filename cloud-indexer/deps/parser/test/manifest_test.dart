// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:parser/cardinality.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parse_error.dart';
import 'package:parser/parser.dart';
import 'package:modular_core/entity/schema.dart' as entity;
import 'package:test/test.dart';

/// An implementation of the parser Importer that loads imported files from
/// dummy names.
class _TestImporter {
  final Map<String, String> files = <String, String>{};

  Future<String> call(final String name) {
    return new Future.value(files[name]);
  }
}

void main() {
  group('Manifest', () {
    const String title = 'ThisIsTheTitle';
    const String icon = 'file://foo/bar';
    const String token1 = 'token1';
    const String token2 = 'token2';
    const String typeToken1 = 'typeToken1';
    const String typeToken2 = 'typeToken2';
    const String arch = 'linux-x64';
    const String modularRevision = 'fbdcf29a4aba9d2325476b2851abd22f0297ae78';
    final Uri verb1 = new Uri.http('verb.tq.io', '1');
    final Uri verb2 = new Uri.http('verb.tq.io', '2');
    final Uri display1 = new Uri.http('display.tq.io', '1');
    final Uri display2 = new Uri.http('display.tq.io', '2');
    final Uri type1 = new Uri.http('type.tq.io', '1');
    final Uri type2 = new Uri.http('type.tq.io', '2');

    final property1 = new Property([new Label.fromUri(type1)].toSet());
    final property1Display =
        new Property([new Label.fromUri(display1)].toSet());
    final property2 = new Property([new Label.fromUri(type2)].toSet());
    final property2Display =
        new Property([new Label.fromUri(display2)].toSet());

    final PathExpr expectedDisplay1 =
        new PathExpr([property1, property1Display]);
    final PathExpr expectedDisplay2 =
        new PathExpr([property2, property2Display]);

    final Uri background_color = new Uri.http('type.tq.io', 'background-color');
    final Uri color = new Uri.http('type.tq.io', 'color');
    final Uri rgb = new Uri.http('type.tq.io', 'rgb');

    // Matcher for parser's ParseError.
    final throwsParseError = throwsA(new isInstanceOf<ParseError>());

    test('Use type representation', () {
      final String yaml = '''
            input:
            - (background-color color) <rgb>
            use:
            - background-color: $background_color
            - color: $color
            - rgb: $rgb
            - verb1: $verb1
      ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.input[0].properties[0].representations,
          equals([new Label.fromUri(rgb)].toSet()));
    });

    test('Parse title', () {
      final String yaml = '''
            title: $title
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.title, equals(title));
    });

    test('Parse icon', () {
      final String yaml = '''
            icon: $icon
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.icon, equals(Uri.parse(icon)));

      // Invalid URL
      final String yaml2 = '''
            icon: foobar
          ''';
      expect(() => parseManifest(yaml2), throwsParseError);

      // icon field is optional
      final String yaml3 = '''
            title: $title
          ''';
      final Manifest manifest2 = parseManifest(yaml3);
      expect(manifest2, isNotNull);
    });

    test('Parse theme-color', () {
      String yaml = '''
          theme-color: '#ffaabc'
        ''';
      Manifest manifest = parseManifest(yaml);
      expect(manifest.themeColor, equals(0xffaabc));

      yaml = '''
        title: $title
        ''';
      manifest = parseManifest(yaml);
      expect(manifest.themeColor, isNull);

      // Invalid values
      yaml = '''
        theme-color: 'ffaabc'
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: '#ffaab'
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: '#ffaabcd'
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: '#ffazbc'
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: #ffaabc
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: ffaabc
      ''';
      expect(() => parseManifest(yaml), throwsParseError);

      yaml = '''
        theme-color: 'ffaabc'
      ''';
      expect(() => parseManifest(yaml), throwsParseError);
    });

    test('Parse arch', () {
      final String yaml = '''
            arch: $arch
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.arch, equals(arch));

      // arch field is optional.
      final String yaml2 = '''
            title: $title
          ''';
      final Manifest manifest2 = parseManifest(yaml2);
      expect(manifest2, isNotNull);

      // Invalid arch.
      final String yaml3 = '''
            arch: arm64
          ''';
      expect(() => parseManifest(yaml3), throwsParseError);
    });

    test('Parse modularRevision', () {
      final String yaml = '''
            modularRevision: $modularRevision
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.modularRevision, equals(modularRevision));

      // modularRevision field is optional.
      final String yaml2 = '''
            title: $title
          ''';
      final Manifest manifest2 = parseManifest(yaml2);
      expect(manifest2, isNotNull);

      // Invalid modularRevision.
      final String yaml3 = '''
            arch: truncated123hash
          ''';
      expect(() => parseManifest(yaml3), throwsParseError);
    });

    test('Parse inline display uri', () {
      final String yaml = '''
            verb: $verb1
            display: $type1 -> $display1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1]));
    });

    test('Parse inline display shorthand', () {
      final String yaml = '''
            verb: $verb1
            display: $typeToken1 -> $token1
            use:
            - $token1: $display1
            - $typeToken1: $type1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1]));
    });

    test('Parse single display uri', () {
      final String yaml = '''
            verb: $verb1
            display:
            - $type1 -> $display1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1]));
    });

    test('Parse single display shorthand', () {
      final String yaml = '''
            verb: $verb1
            display:
            - $typeToken1 -> $token1
            use:
            - $token1: $display1
            - $typeToken1: $type1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1]));
    });

    test('Parse multiple display uri', () {
      final String yaml = '''
            verb: $verb1
            display:
            - $type1 -> $display1
            - $type2 -> $display2
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1, expectedDisplay2]));
    });

    test('Parse multiple display shorthand', () {
      final String yaml = '''
            verb: $verb1
            display:
            - $typeToken1 -> $token1
            - $typeToken2 -> $token2
            use:
            - $token1: $display1
            - $token2: $display2
            - $typeToken1: $type1
            - $typeToken2: $type2
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.display, equals([expectedDisplay1, expectedDisplay2]));
    });

    test('Parse inline compose uri', () {
      final String yaml = '''
            verb: $verb1
            compose: $type1 -> $display1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1]));
    });

    test('Parse inline compose shorthand', () {
      final String yaml = '''
            verb: $verb1
            compose: $typeToken1 -> $token1
            use:
            - $token1: $display1
            - $typeToken1: $type1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1]));
    });

    test('Parse single compose uri', () {
      final String yaml = '''
            verb: $verb1
            compose:
            - $type1 -> $display1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1]));
    });

    test('Parse single compose shorthand', () {
      final String yaml = '''
            verb: $verb1
            compose:
            - $typeToken1 -> $token1
            use:
            - $token1: $display1
            - $typeToken1: $type1
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1]));
    });

    test('Parse multiple compose uri', () {
      final String yaml = '''
            verb: $verb1
            compose:
            - $type1 -> $display1
            - $type2 -> $display2
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1, expectedDisplay2]));
    });

    test('Parse multiple compose shorthand', () {
      final String yaml = '''
            verb: $verb1
            compose:
            - $typeToken1 -> $token1
            - $typeToken2 -> $token2
            use:
            - $token1: $display1
            - $token2: $display2
            - $typeToken1: $type1
            - $typeToken2: $type2
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.compose, equals([expectedDisplay1, expectedDisplay2]));
    });

    test('Parse multiple', () {
      final String yaml = '''
verb: $verb1
---
verb: $verb2
''';
      final List<Manifest> manifest = parseManifests(yaml);
      expect(manifest.length, equals(2));
      expect(manifest[0].verb.label.uri, equals(verb1));
      expect(manifest[1].verb.label.uri, equals(verb2));
    });

    test('Flatten input and output', () {
      final String yaml = '''
            verb: $verb1
            input: $token1
            output: $token2

            use:
            - $token1: $display1
            - $token2: $display2
          ''';
      final Manifest manifest = parseManifest(yaml);
      expect(manifest.input.length, equals(1));
      expect(manifest.output.length, equals(1));
    });

    test('Serialization', () {
      final String yaml = '''
            title: hello
            url: $verb2
            verb: $verb1
            input: $token1
            output: $token2
            display: $type1 -> $display1
            compose: $type1 -> $display2
            arch: $arch
            modularRevision: $modularRevision

            use:
            - $token1: $type1
            - $token2: $type2
          ''';
      final Manifest manifest = new Manifest.parseYamlString(yaml);
      final Manifest otherManifest =
          new Manifest.fromJsonString(manifest.toJsonString());
      expect(manifest, equals(otherManifest));
    });

    test('Import', () async {
      final _TestImporter importer = new _TestImporter();
      importer.files['base.yaml'] = '''
            input:
            - (background-color color) <rgb>
            import:
            - import.yaml
      ''';
      importer.files['./import.yaml'] = '''
            use:
            - background-color: $background_color
            - color: $color
            - rgb: $rgb
            - verb1: $verb1
      ''';

      final Manifest manifest = await parseManifestFile('base.yaml', importer);
      expect(manifest.input[0].properties[0].representations,
          equals([new Label.fromUri(rgb)].toSet()));
    });

    test('Schema, definition', () {
      final String rgbColorType = 'http://tq.io/schema/rgbColor';
      // Also exercise the Schema type being defined in the 'use:' section.
      final String yaml = '''
            input:
            - rgbColor
            schema:
            - type: rgbColor
              properties:
                - name: red
                  type: int
                - name: green
                  type: int
                - name: blue
                  type: int
                  isRepeated: true  # for testing cardinality of translated
                                    # expression
            use:
              - rgbColor: $rgbColorType
      ''';
      final Manifest manifest = parseManifest(yaml);

      // We should see the schema that we defined available.
      expect(manifest.schemas.length, equals(1));
      final entity.Schema schema = manifest.schemas[0];
      expect(schema.type, equals(rgbColorType));
      expect(schema.properties.length, equals(3));

      expect(manifest.input[0].properties.length, equals(2));
      // NOTE: The parser does not currently include the represenation label
      // for the scalar property values in the expressions. It is a) simpler
      // this way, and b) makes no difference at this time.

      // rgbColor -> red
      expect(
          manifest.input[0].properties,
          equals([
            new Property([new Label.fromUri(Uri.parse(schema.type))],
                Cardinality.singular, []),
            new Property(
                [new Label.fromUri(Uri.parse(schema.propertyLongName('red')))],
                Cardinality.optional,
                [])
          ]));
      // rgbColor -> green
      expect(
          manifest.input[1].properties,
          equals([
            new Property([new Label.fromUri(Uri.parse(schema.type))],
                Cardinality.singular, []),
            new Property(
                [
                  new Label.fromUri(Uri.parse(schema.propertyLongName('green')))
                ],
                Cardinality.optional,
                [] /* representation labels */)
          ]));
      // rgbColor -> blue
      expect(
          manifest.input[2].properties,
          equals([
            new Property([new Label.fromUri(Uri.parse(schema.type))],
                Cardinality.singular, []),
            new Property(
                [new Label.fromUri(Uri.parse(schema.propertyLongName('blue')))],
                Cardinality.optionalRepeated, // isRepeated = true
                [])
          ]));
    });

    test('Schema, child of expression', () {
      final String fooType = 'http://tq.io/type/foo';
      final String schemaType = 'http://tq.io/schema/thing';
      // Also exercise the Schema type when it is in the 'use:' section.
      final String yaml = '''
            input:
            - $fooType -> $schemaType
            schema:
            - type: $schemaType
              properties:
                - name: a_number
                  type: int
      ''';
      final Manifest manifest = parseManifest(yaml);

      final schema = manifest.schemas[0];

      expect(manifest.input[0].properties.length, equals(3));

      expect(
          manifest.input[0].properties,
          equals([
            new Property(
                [new Label.fromUriString(fooType)], Cardinality.singular, []),
            new Property([new Label.fromUriString(schemaType)],
                Cardinality.singular, []),
            new Property(
                [
                  new Label.fromUri(
                      Uri.parse(schema.propertyLongName('a_number')))
                ],
                Cardinality.optional,
                [])
          ]));
    });

    test('Schema, complex', () {
      final String thingType = 'http://tq.io/schema/thing';
      final String anotherThingType = 'http://tq.io/schema/anotherType';
      final String yaml = '''
            input:
            - $thingType
            schema:
            - type: $anotherThingType
              properties:
                - name: foo
                  type: int
            - type: $thingType
              properties:
                - name: thing
                  type: $anotherThingType
      ''';
      final Manifest manifest = parseManifest(yaml);

      // We should see the schema that we defined available.
      expect(manifest.schemas.length, equals(2));
      final entity.Schema anotherSchema = manifest.schemas[0];
      expect(anotherSchema.type, equals(anotherThingType));
      final entity.Schema schema = manifest.schemas[1];
      expect(schema.type, equals(thingType));

      expect(manifest.input[0].properties.length, equals(3));
      expect(
          manifest.input[0].properties,
          equals([
            new Property([new Label.fromUri(Uri.parse(schema.type))],
                Cardinality.singular, []),
            new Property(
                [
                  new Label.fromUri(Uri.parse(schema.propertyLongName('thing')))
                ],
                Cardinality.optional,
                []),
            new Property(
                [
                  new Label.fromUri(
                      Uri.parse(anotherSchema.propertyLongName('foo')))
                ],
                Cardinality.optional,
                [])
          ]));
    });

    test('Schema, recursive', () {
      final String thingType = 'http://tq.io/schema/thing';
      final String yaml = '''
            input:
            - $thingType
            schema:
            - type: $thingType
              properties:
                - name: thing
                  type: $thingType
      ''';

      expect(() => parseManifest(yaml), throws);
    });

    test('Schema, imported', () async {
      // Schemas imported from other Manifests will be included in
      // [Manifest.schemas].
      final _TestImporter importer = new _TestImporter();
      importer.files['base.yaml'] = '''
            input:
            - http://tq.io/schema/rgbColor
            import:
            - import.yaml
      ''';
      importer.files['./import.yaml'] = '''
            schema:
            - type: http://tq.io/schema/rgbColor
              properties:
                - name: red
                  type: int
                - name: green
                  type: int
                - name: blue
                  type: int
      ''';
      final Manifest manifest = await parseManifestFile('base.yaml', importer);
      expect(manifest.schemas.length, equals(1));
      expect(manifest.schemas[0].type, equals('http://tq.io/schema/rgbColor'));
    });
  });
}
