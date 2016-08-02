// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'entity.dart';

/// A [SchemaRegistry] is a collection of [Schema] objects. It is used when
/// creating [Entity] objects, which reference type strings.
class SchemaRegistry {
  final Map<String, Schema> _schemas = <String, Schema>{};
  final Map<String, Schema> _aliases = <String, Schema>{};

  static final SchemaRegistry global = new SchemaRegistry();

  /// Adds [schema] to this registry. If [aliases] are given, returns this
  /// Schema if get(alias) is called. Aliases that are already present will
  /// throw an [ArgumentError].
  void add(final Schema schema, [final Iterable<String> aliases]) {
    _schemas[schema.type] = schema;
    aliases?.forEach((final String alias) {
      if (_aliases.containsKey(alias)) {
        throw new ArgumentError("Schema alias $alias is already associated "
            "with: ${_aliases[alias]}");
      }
      _aliases[alias] = schema;
    });
  }

  /// Returns the [Schema] associated with [type].
  Schema get(final String type) {
    return _schemas[type] ?? _aliases[type];
  }
}

/// Defines a data schema for an [Entity].
class Schema {
  /// The [type] is the unique identifier for this [Schema].
  final String type;
  List<Property> properties;

  factory Schema.fromJsonString(final String jsonString) =>
      new Schema.fromJson(JSON.decode(jsonString));

  factory Schema.fromJson(dynamic json) {
    final String type = json['type'];
    final List<Property> properties = json['properties']
        .map((final dynamic property) => new Property.fromJson(property))
        .toList();
    return new Schema(type, properties);
  }

  /// Creates a new [Schema] with the given [type] and map of named
  /// [properties]. If [autoPublish] is true, the [Schema] calls [publish()] on
  /// itself, making it available in the global [SchemaRegistry].
  Schema(this.type, this.properties, {bool autoPublish: false}) {
    if (autoPublish) publish();
  }

  void publish([SchemaRegistry registry]) {
    if (registry == null) registry = SchemaRegistry.global;
    registry.add(this);
  }

  /// Returns the [Property] associated with [name] or null if not found.
  Property property(final String name) {
    return properties.firstWhere((final Property p) => p.name == name,
        orElse: () => null);
  }

  /// Returns the long property name for [name], which contains both the
  /// full Schema type name as well as the property.
  String propertyLongName(final String name) => '$type#$name';

  String toJsonString() => JSON.encode(toJson());

  dynamic toJson() => {'type': type, 'properties': properties};

  @override
  String toString() {
    final List<String> propertyStrings = [];
    properties.forEach((final Property prop) {
      propertyStrings.add('  $prop');
    });
    final String collapsedProperties = propertyStrings.join('\n');
    return '$runtimeType: $type\n$collapsedProperties';
  }
}

/// Defines the characteristics of a single property on a [Schema].
///
/// The following types are built in: int, string, float and datetime.
class Property {
  // Either a type defined by another Schema or one of the built-in types.
  final String name;
  final String type;
  final bool isRepeated;

  // TODO(thatguy): Add parse validation.
  factory Property.fromJson(dynamic value) =>
      new Property(value['name'], value['type'],
          isRepeated: value['isRepeated'] ?? false);

  Property(this.name, this.type, {this.isRepeated: false}) {
    assert(name != null);
    assert(type != null);
    assert(isRepeated != null);
  }

  factory Property.int(final String name, {bool isRepeated: false}) =>
      new Property(name, 'int', isRepeated: isRepeated);
  factory Property.float(final String name, {bool isRepeated: false}) =>
      new Property(name, 'float', isRepeated: isRepeated);
  factory Property.string(final String name, {bool isRepeated: false}) =>
      new Property(name, 'string', isRepeated: isRepeated);
  factory Property.dateTime(final String name, {bool isRepeated: false}) =>
      new Property(name, 'datetime', isRepeated: isRepeated);

  bool get isBuiltin => ['int', 'float', 'string', 'datetime'].contains(type);

  bool validateValue(dynamic value) {
    switch (type) {
      case 'int':
        return value is int;
      case 'float':
        return value is double;
      case 'string':
        return value is String;
      case 'datetime':
        return value is DateTime;
      default:
        return value is Entity && value.types.contains(type);
    }
  }

  Uint8List encode(dynamic value) {
    String stringValue;
    switch (type) {
      case 'int':
      case 'float':
      case 'string':
        stringValue = value.toString();
        break;
      case 'datetime':
        stringValue = value.toIso8601String();
        break;
      default:
        // TODO(thatguy): Better exception type.
        throw "FATAL: Called $runtimeType.encode() on a non-builtin type.";
    }

    return new Uint8List.fromList(stringValue.codeUnits);
  }

  dynamic decode(final Uint8List value) {
    if (value == null) return null;
    return decodeString(new String.fromCharCodes(value.toList()));
  }

  dynamic decodeString(final String stringValue) {
    switch (type) {
      case 'int':
        return int.parse(stringValue);
      case 'float':
        return double.parse(stringValue);
      case 'string':
        return stringValue;
      case 'datetime':
        return DateTime.parse(stringValue);
      default:
        // TODO(thatguy): Better exception type.
        throw "FATAL: Called $runtimeType.decode() on a non-builtin type.";
    }
  }

  dynamic encodeJson(dynamic value) {
    if (isBuiltin) {
      return new String.fromCharCodes(encode(value));
    } else {
      assert(value is Entity);
      return value.toJsonWithoutSchemas();
    }
  }

  dynamic toJson() => {'name': name, 'type': type, 'isRepeated': isRepeated};

  @override
  String toString() => '$name: $type (isRepeated: $isRepeated)';
}
