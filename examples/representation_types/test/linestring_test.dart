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
      Uri.parse("http://geojson.org/geojson-spec.html#linestring");

  test("LineString constructor", () {
    LineString l = new LineString(points);
    expect(l.points, equals(points));
  });

  test("Bindings", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    // Create a LineString
    LineString l1 = new LineString(points);
    // Convert it to a representation value.
    RepresentationValue value = registry.write(l1);
    // Check the label.
    expect(value.label, equals(labelUri));
    // Convert it back.
    dynamic readValue = registry.read(value);
    // Check the dart data type.
    expect(readValue, new isInstanceOf<LineString>());
    LineString l2 = readValue as LineString;
    // Make sure it's the same value.
    expect(l2, equals(l1));
    // TODO(ianloic): check that the generated JSON is spec compliant.
  });

  test("Parse", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    String json = '''{ "type": "LineString",
    "coordinates": [ [100.0, 0.0], [101.0, 1.0] ]
    }''';
    LineString l =
        registry.read(new RepresentationValue(labelUri, UTF8.encode(json)));
    expect(l, new isInstanceOf<LineString>());
    expect(l.points.length, equals(2));
    expect(l.points[0], equals(new Point(0.0, 100.0)));
    expect(l.points[1], equals(new Point(1.0, 101.0)));
  });
}
