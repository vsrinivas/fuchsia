// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show UTF8;

import "package:representation_types/geo.dart";
import 'package:modular/representation_types.dart';
import "package:test/test.dart";

void main() {
  List<Point> points = <Point>[
    new Point(29.979175, 31.134358),
    new Point(32.5355, 44.4275),
    new Point(37.949722, 27.363889),
    new Point(37.637861, 21.63),
    new Point(37.0379, 27.4241),
    new Point(36.451111, 28.227778),
    new Point(31.213889, 29.885556),
  ];

  final Uri labelUri =
      Uri.parse("http://geojson.org/geojson-spec.html#polygon");

  test("Polygon constructor", () {
    Polygon p1 = new Polygon.fromOutline(points);

    expect(p1.outline, equals(points));
    expect(p1.holes, equals([]));
  });

  test("Bindings", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    // Create a Polygon.
    Polygon p1 = new Polygon.fromOutline(points);
    // Convert it to a representation value.
    RepresentationValue value = registry.write(p1);
    // Check the label.
    expect(value.label, equals(labelUri));
    // Convert it back.
    dynamic readValue = registry.read(value);
    // Check the dart data type.
    expect(readValue, new isInstanceOf<Polygon>());
    Polygon p2 = readValue as Polygon;
    // Make sure it's the same value.
    expect(p2, equals(p1));

    // TODO(ianloic): check that the generated JSON is spec compliant.
  });

  test("ParseNoHoles", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    String json = '''{ "type": "Polygon",
      "coordinates": [
        [ [100.0, 0.0], [101.0, 0.0], [101.0, 1.0], [100.0, 1.0], [100.0, 0.0] ]
        ]
     }''';
    Polygon p =
        registry.read(new RepresentationValue(labelUri, UTF8.encode(json)));
    expect(p, new isInstanceOf<Polygon>());
    expect(p.outline.length, equals(5));
    expect(p.holes.length, equals(0));
  });

  test("ParseHoles", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    String json = '''{ "type": "Polygon",
    "coordinates": [
      [ [100.0, 0.0], [101.0, 0.0], [101.0, 1.0], [100.0, 1.0], [100.0, 0.0] ],
      [ [100.2, 0.2], [100.8, 0.2], [100.8, 0.8], [100.2, 0.8], [100.2, 0.2] ]
      ]
   }''';
    Polygon p =
        registry.read(new RepresentationValue(labelUri, UTF8.encode(json)));
    expect(p, new isInstanceOf<Polygon>());
    expect(p.outline.length, equals(5));
    expect(p.holes.length, equals(1));
    expect(p.holes[0].length, equals(5));
  });
}
