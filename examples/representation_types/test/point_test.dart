// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show UTF8;

import "package:representation_types/geo.dart";
import 'package:modular/representation_types.dart';
import "package:test/test.dart";

void main() {
  final Uri labelUri = Uri.parse("http://geojson.org/geojson-spec.html#point");

  test("Point constructors", () {
    Point p1 = new Point(29.979175, 31.134358);
    expect(p1.latitude, equals(29.979175));
    expect(p1.longitude, equals(31.134358));
  });

  test("toCsv", () {
    Point p = new Point(37.949722, 27.363889);
    expect(p.toCsv(), equals("37.949722,27.363889"));
  });

  test("translate", () {
    Point p = new Point(100.0, 50.0);
    Point t = p.translate(5.0, 10.0);
    // The original point shouldn't change.
    expect(p.latitude, equals(100.0));
    expect(p.longitude, equals(50.0));
    // The new one should be in a different spot.
    expect(t.latitude, equals(105.0));
    expect(t.longitude, equals(60.0));
  });

  test("midpoint", () {
    Point p = new Point(50.0, 60.0).midpoint(new Point(150.0, 140.0));
    expect(p, equals(new Point(100.0, 100.0)));
  });

  test("Bindings", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    // Create a point.
    Point p1 = new Point(37.637861, 21.63);
    // Convert it to a representation value.
    RepresentationValue value = registry.write(p1);
    // Check the label.
    expect(value.label, equals(labelUri));
    // Convert it back.
    dynamic readValue = registry.read(value);
    // Check the dart data type.
    expect(readValue, new isInstanceOf<Point>());
    Point p2 = readValue as Point;
    // Make sure it's the same value.
    expect(p2, equals(p1));

    // TODO(ianloic): check that the generated JSON is spec compliant.
  });

  test("Parse", () {
    // Create a binding registry and register the geo types in it.
    RepresentationBindingsRegistry registry =
        new RepresentationBindingsRegistry();
    registerTypes(registry);

    String json = '''{ "type": "Point", "coordinates": [100.0, 0.0] }''';
    Point p =
        registry.read(new RepresentationValue(labelUri, UTF8.encode(json)));
    expect(p, new isInstanceOf<Point>());
    expect(p.latitude, equals(0.0));
    expect(p.longitude, equals(100.0));
  });
}
