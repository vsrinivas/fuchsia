// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:math' show min, max;
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:modular/representation_types.dart';

/// Base class for implementing GeoJSON geometry objects, see:
/// http://geojson.org/geojson-spec.html#geometry-objects
abstract class _GeoJsonGeometry<C> {
  final C coordinates;
  const _GeoJsonGeometry(this.coordinates);
  Map<String, dynamic> toJson() =>
      {"type": this.runtimeType.toString(), "coordinates": coordinates,};

  @override
  bool operator ==(other) {
    return other is _GeoJsonGeometry<C> &&
        const DeepCollectionEquality().equals(coordinates, other.coordinates);
  }

  @override
  int get hashCode => const DeepCollectionEquality().hash(coordinates);
}

class Point extends _GeoJsonGeometry<List<double>> {
  Point(final double latitude, final double longitude)
      : super(<double>[longitude, latitude]);

  /// Make a |Point| object from a GeoJSON coordinate list.
  Point.fromCoordinates(List<double> coordinates) : super(coordinates) {
    assert(coordinates.length == 2);
  }

  /// Make a |Point| object from a GeoJSON JSON object.
  factory Point.fromJson(Map<String, dynamic> json) {
    List<double> coordinates = json["coordinates"];
    assert(coordinates.length == 2);
    return new Point.fromCoordinates(json["coordinates"]);
  }

  static List<Point> listFromCoordinates(List<List<double>> coordinates) =>
      coordinates
          .map((List<double> ll) => new Point.fromCoordinates(ll))
          .toList(growable: false);

  /// The latitude of the point.
  double get latitude => coordinates[1];

  /// The longitude of the point.
  double get longitude => coordinates[0];

  /// Simple getter for "latitude,longitude" - useful for passing to external
  /// APIs.
  String toCsv() => "$latitude,$longitude";

  Point translate(double deltaLatitude, double deltaLongitude) {
    return new Point(latitude + deltaLatitude, longitude + deltaLongitude);
  }

  /// Return a point half way between this one and another.
  Point midpoint(Point other) => new Point(
      (latitude + other.latitude) / 2, (longitude + other.longitude) / 2);

  @override
  String toString() => "Point<lat=$latitude long=$longitude>";
}

class LineString extends _GeoJsonGeometry<List<List<double>>> {
  factory LineString(Iterable<Point> points) {
    return new LineString.fromCoordinates(
        points.map((Point p) => p.coordinates).toList(growable: false));
  }

  LineString.fromCoordinates(List<List<double>> coordinates)
      : super(coordinates);

  List<Point> get points => Point.listFromCoordinates(coordinates);

  double get minLatitude => points.map((Point p) => p.latitude).reduce(min);
  double get maxLatitude => points.map((Point p) => p.latitude).reduce(max);
  double get minLongitude => points.map((Point p) => p.longitude).reduce(min);
  double get maxLongitude => points.map((Point p) => p.longitude).reduce(max);

  @override
  String toString() => "LineString<${points.join(" ")}>";
}

class Polygon extends _GeoJsonGeometry<List<List<List<double>>>> {
  Polygon.fromCoordinates(List<List<List<double>>> coordinates)
      : super(coordinates);

  factory Polygon.fromOutline(List<Point> outline) =>
      new Polygon.fromCoordinates(
          [outline.map((Point p) => p.coordinates).toList(growable: false)]);

  List<Point> get outline => Point.listFromCoordinates(coordinates[0]);

  List<List<Point>> get holes => coordinates
      .sublist(1)
      .map(Point.listFromCoordinates)
      .toList(growable: false);

  @override
  String toString() =>
      "Polygon<outline=${outline.toString()} holes=${holes.toString()}>";
}

typedef T _GeoJsonConstructor<C, T>(C coordinates);

class _GeoJsonBindings<C, T extends _GeoJsonGeometry<C>>
    implements RepresentationBindings<T> {
  final _GeoJsonConstructor<C, T> constructor;

  const _GeoJsonBindings(this.constructor);

  @override
  String get label =>
      "http://geojson.org/geojson-spec.html#${T.toString().toLowerCase()}";

  @override
  T decode(Uint8List data) {
    Map<String, dynamic> json = JSON.decode(UTF8.decode(data));

    assert(json.containsKey("type"));
    assert(json["type"] == T.toString());
    assert(json.containsKey("coordinates"));
    assert(json["coordinates"] is C);

    return constructor(json["coordinates"]);
  }

  @override
  Uint8List encode(T value) {
    return UTF8.encode(JSON.encode(value.toJson()));
  }
}

void registerTypes(RepresentationBindingsRegistry registry) {
  registry.register(
      Point,
      new _GeoJsonBindings<List<double>, Point>((List<double> coordinates) =>
          new Point.fromCoordinates(coordinates)));
  registry.register(
      LineString,
      new _GeoJsonBindings<List<List<double>>, LineString>(
          (List<List<double>> coordinates) =>
              new LineString.fromCoordinates(coordinates)));
  registry.register(
      Polygon,
      new _GeoJsonBindings<List<List<List<double>>>, Polygon>(
          (List<List<List<double>>> coordinates) =>
              new Polygon.fromCoordinates(coordinates)));
}
