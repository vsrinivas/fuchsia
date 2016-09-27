// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';
import 'dart:convert';
import 'dart:typed_data';

import 'schema.dart';
import '../graph/graph.dart';

/// [Entity] represents a data structure that conforms to one or more [Schema]s,
/// as identified by the [types] parameter to the constructor.
///
/// Value type validation is performed, with exceptions being thrown when an
/// action is taken that does not conform to one of the [Schema]s that the
/// [Entity] models.
///
/// You can read and write properties using the [] operator, like so:
///
///   myEntity['myProperty'] = 10;
///   myEntity['otherThing'] = otherEntity;
///
/// Saving and loading an entity to/from a Graph is as simple as:
///
///   graph.mutate((final GraphMutator mutator) {
///     myEntity.save(mutator);
///   });
///   final NodeId myEntityId = myEntity.id;
///   // or
///   final Node myEntityNode = myEntity.node;
///
///   // later...
///   final Entity myLoadedEntity = new Entity.fromNode(myEntityNode);
class Entity {
  /// The [Schema] types this [Entity] models.
  final List<String> _schemaTypes;

  /// For each [Property] in each [Schema] in [_schemaTypes], one
  /// [_PropertyValue] object is added to this list. It stores the current
  /// value(s) and performs validation and storage management.
  final List<_PropertyValue> _values = [];

  /// Maps from a [Property]'s short name (the name given in the [Schema]) and
  /// long name to the respective [_PropertyValue] object.
  final Map<String, _PropertyValue> _propertyValueMap = {};

  /// The [Node] in the [Graph] where our data is stored.
  Node _node;

  /// The [SchemaRegistry] we were initialized with.
  SchemaRegistry registry;

  /// Metadata about this [Entity], such as important timestamps. This gets
  /// saved and loaded at the [Entity]'s root node.
  _Metadata _metadata;

  DateTime get creationTime => _metadata.creationTime;
  DateTime get modifiedTime => _metadata.modifiedTime;

  /// For an [Entity] that has already been [save()]ed at [node], returns the
  /// list of [Schema] types used to construct it. Returns empty list otherwise.
  static List<String> typesFromNode(final Node node) {
    return _loadTypesFromNode(node);
  }

  /// Constructs a new [Entity] object that will conform to the [Schema]s
  /// specified by [_schemaTypes]. If a [registry] is given, it will use
  /// it to find the [Schema] descriptions for the types specified.
  Entity._internal(this._schemaTypes, this.registry, {final _Metadata metadata})
      : _metadata = metadata ?? new _Metadata() {
    _initializeProperties();
  }

  Entity(final List<String> schemaTypes, {final SchemaRegistry registry})
      : this._internal(schemaTypes, registry);

  /// Constructs a new [Entity] object from a [Node], and loads values for
  /// properties automatically. Values that themselves are [Entity]s are
  /// eagerly loaded from the [Graph].
  Entity.fromNode(final Node node, {this.registry})
      : _node = node,
        _schemaTypes = _loadTypesFromNode(node),
        _metadata = _loadMetadataFromNode(node) {
    if (_schemaTypes.isEmpty) {
      // TODO(thatguy): A better exception would be good.
      throw new Exception("No Entity found stored at $node");
    }
    _initializeProperties();
    _loadPropertiesFromNode({} /* nodeEntityMap */);
  }

  /// Used internally when loading [Entity] values. Acts similarly to
  /// [fromNode()] above, but also stores itself in [nodeEntityMap] to aid in
  /// loading Entities that circularly reference each other.
  Entity._fromNode(
      final Node node, this.registry, Map<Node, Entity> nodeEntityMap)
      : _node = node,
        _schemaTypes = _loadTypesFromNode(node),
        _metadata = _loadMetadataFromNode(node) {
    if (_schemaTypes.isEmpty) {
      // TODO(thatguy): A better exception would be good.
      throw new Exception("No Entity found stored at $node");
    }
    nodeEntityMap[node] = this;
    _initializeProperties();
    _loadPropertiesFromNode(nodeEntityMap);
  }

  factory Entity.fromJsonString(String string, {SchemaRegistry registry}) {
    return new Entity.fromJson(JSON.decode(string), registry: registry);
  }

  factory Entity.fromJson(Map<String, dynamic> json,
          {SchemaRegistry registry}) =>
      new Entity._fromJsonWithSchemasAndMetadata(
          json['values'], json['schemas'],
          metadata: new _Metadata.fromJson(json['metadata']),
          registry: registry);

  factory Entity.fromJsonWithoutMetadata(Map<String, dynamic> json,
          {SchemaRegistry registry}) =>
      new Entity._fromJsonWithSchemasAndMetadata(
          json['values'], json['schemas'],
          registry: registry);

  factory Entity.fromJsonWithSchemas(
          Map<String, dynamic> json, List<String> schemas,
          {SchemaRegistry registry}) =>
      new Entity._fromJsonWithSchemasAndMetadata(json, schemas,
          registry: registry);

  factory Entity._fromJsonWithSchemasAndMetadata(
      Map<String, dynamic> json, List<String> schemas,
      {_Metadata metadata, SchemaRegistry registry}) {
    Entity e = new Entity._internal(schemas, registry, metadata: metadata);
    json.forEach((String name, dynamic value) {
      e._propertyValueMap[name].loadJson(value);
    });
    return e;
  }

  Iterable<String> get types => _schemaTypes;

  Node get node => _node;
  NodeId get id => _node?.id;

  /// Returns the value associated with [propertyName] on this Entity. If
  /// [propertyName] is not defined in one of the [Schema]s in [types], throws
  /// an [ArgumentError].
  dynamic operator [](String propertyName) {
    final _PropertyValue v = _getPropertyValueOrThrow(propertyName);
    return v.value;
  }

  /// Sets the value associated with [propertyName] on this Entity to [value].
  /// If [propertyName] is not defined in one of the [Schema]s in [types], or
  /// if [value] does not validate as a type appropriate for assigning to the
  /// parameter [propertyName], throws an [ArgumentError].
  void operator []=(String propertyName, dynamic value) {
    // Update time of last modification.
    _metadata.modifiedTime = new DateTime.now();

    final _PropertyValue v = _getPropertyValueOrThrow(propertyName);
    v.set(value);
  }

  /// Saves this entity to a [Graph] by using the given [mutator].
  /// Note that values that themselves are [Entity]s and that have not been
  /// [save()]ed before will have [save()] called so that they are assigned
  /// a [Node] and can be referenced.
  ///
  /// [entityNodeMap] is used internally and should not be provided.
  ///
  /// TODO(thatguy): It might be better/more natural to *always* recursively
  /// save() on Entity values.
  void save(final GraphMutator mutator, [Map<Entity, Node> entityNodeMap]) {
    if (_node == null) {
      _node = mutator.addNode();
      _saveTypesToNode(mutator, node, _schemaTypes);
    }

    if (entityNodeMap == null) entityNodeMap = {};
    entityNodeMap[this] = _node;

    // Save all property values.
    _values.forEach((final _PropertyValue property) {
      property.save(mutator, _node, entityNodeMap);
    });

    _saveMetadataToNode(mutator, node, _metadata);
  }

  /// Reloads property values from the [Node] from which this Entity was loaded.
  /// Calling reload() when the underlying [Node] no longer exists unassociates
  /// the [Entity] from the Graph.
  ///
  /// TODO(thatguy): Document what happens to Entity references that are
  /// property values.
  void reload() {
    if (_node == null) return;
    if (_node.isDeleted) {
      _node = null;
      // Reset everything.
      _initializeProperties();
      return;
    }
    _loadPropertiesFromNode({} /* nodeEntityMap */);

    _metadata = _loadMetadataFromNode(_node);
  }

  /// Removes all values (except values that are [Entity]s) and their respective
  /// property edges, as well as the [Node] that identifies the [Entity].
  void delete(final GraphMutator mutator) {
    if (_node == null) return;

    // Remove all property edges and nodes.
    _values.forEach((final _PropertyValue property) {
      property.delete(mutator);
    });

    mutator.removeNode(_node.id);
  }

  @override
  String toString() => '$runtimeType($_schemaTypes)';

  // Note: entity cycles aren't supported yet.
  Map<String, dynamic> toJson() => {
        'schemas': _schemaTypes,
        'metadata': _metadata.toJson(),
        'values': toJsonOnlyValues()
      };

  Map<String, dynamic> toJsonOnlyValues() =>
      new Map.fromIterable(_values.where((_PropertyValue p) => p.hasValue()),
          key: (_PropertyValue p) => p.propertyName,
          value: (_PropertyValue p) => p.toJson());

  Map<String, dynamic> toJsonWithoutMetadata() =>
      {'schemas': _schemaTypes, 'values': toJsonOnlyValues()};

  void _initializeProperties() {
    _values.clear();
    if (registry == null) registry = SchemaRegistry.global;
    for (String schemaType in _schemaTypes) {
      final Schema schema = registry.get(schemaType);
      if (schema == null) {
        // TODO(thatguy): A better exception would probably be good.
        throw "Could not find schema for type: $schemaType";
      }
      schema.properties.forEach((final Property property) {
        final String longName = schema.propertyLongName(property.name);
        final _PropertyValue propertyValue =
            new _PropertyValue(longName, property, registry);
        _values.add(propertyValue);

        // Add to the lookup table for property name -> value.
        _propertyValueMap[longName] = propertyValue;
        // For short name collisions, prioritize the first Schema to mention it.
        if (!_propertyValueMap.containsKey(property.name)) {
          _propertyValueMap[property.name] = propertyValue;
        }
      });
    }
  }

  void _loadPropertiesFromNode(final Map<Node, Entity> nodeEntityMap) {
    _values.forEach((final _PropertyValue value) {
      List<Edge> propertyEdges =
          _node.outEdgesWithLabels([value.propertyName]).toList();
      value.load(propertyEdges, nodeEntityMap);
    });
  }

  _PropertyValue _getPropertyValueOrThrow(String propertyName) {
    final _PropertyValue v = _propertyValueMap[propertyName];
    if (v == null) {
      throw new ArgumentError("Property ${propertyName} does not exist.");
    }
    return v;
  }

  String toJsonString() => JSON.encode(this.toJson());
}

/// Stores metadata about an [Entity].
class _Metadata {
  static const String _creationTimeLabel = "creationTime";
  static const String _modifiedTimeLabel = "modifiedTime";

  final DateTime creationTime;
  DateTime modifiedTime;

  _Metadata() : creationTime = new DateTime.now() {
    modifiedTime = creationTime;
  }

  _Metadata._internal(this.creationTime, this.modifiedTime);

  _Metadata.fromJson(final Map<String, dynamic> json)
      : this._internal(json[_creationTimeLabel], json[_modifiedTimeLabel]);

  _Metadata.fromJsonString(final String jsonString)
      : this.fromJson(JSON.decode(jsonString, reviver: (key, value) {
          if (key == _creationTimeLabel || key == _modifiedTimeLabel)
            return DateTime.parse(value);
          return value;
        }));

  Map<String, dynamic> toJson() =>
      {_creationTimeLabel: creationTime, _modifiedTimeLabel: modifiedTime};

  String toJsonString() =>
      JSON.encode(toJson(), toEncodable: (o) => o.toIso8601String());
}

abstract class _PropertyValue {
  final String propertyName;
  final Property property;
  final SchemaRegistry registry;

  _PropertyValue._(this.propertyName, this.property, this.registry);

  factory _PropertyValue(final String name, final Property property,
      final SchemaRegistry registry) {
    if (property.isRepeated) {
      return new _RepeatedPropertyValue(name, property, registry);
    }
    return new _SinglePropertyValue(name, property, registry);
  }

  dynamic get value;
  bool hasValue();
  void set(dynamic value);
  void load(List<Edge> edges, Map<Node, Entity> nodeEntityMap);
  void save(GraphMutator mutator, Node parentNode,
      final Map<Entity, Node> entityNodeMap);
  void delete(GraphMutator mutator);

  dynamic toJson();
  void loadJson(dynamic value);
}

class _SinglePropertyValue extends _PropertyValue {
  dynamic _value = null;

  _PropertyValueStorage _storage;

  _SinglePropertyValue(
      final String name, final Property property, final SchemaRegistry registry)
      : super._(name, property, registry) {
    _storage = new _PropertyValueStorage([name], property, registry);
  }

  dynamic get value => _value;

  @override
  bool hasValue() => _value != null;

  void set(dynamic value) {
    if (value != null && !property.validateValue(value)) {
      final String type =
          value is Entity ? 'Entity(${value.types})' : value.runtimeType;
      throw new ArgumentError("Cannot use a value of type ${type} "
          "to set a property $propertyName of type ${property.type}");
    }
    _value = value;
  }

  void load(final List<Edge> edges, final Map<Node, Entity> nodeEntityMap) {
    _value = _storage.load(edges, nodeEntityMap);
  }

  void save(final GraphMutator mutator, final Node parentNode,
      final Map<Entity, Node> entityNodeMap) {
    _storage.save(mutator, _value, parentNode, entityNodeMap);
  }

  void delete(final GraphMutator mutator) {
    _storage.delete(mutator);
  }

  @override
  dynamic toJson() => property.encodeJson(value);

  @override
  void loadJson(value) {
    if (property.isBuiltin) {
      assert(value is String);
      set(property.decodeString(value));
    } else {
      set(new Entity.fromJsonWithSchemas(value, [property.type]));
    }
  }
}

const String _indexLabelPrefix = 'index:';

class _RepeatedPropertyValue extends _PropertyValue with ListMixin<dynamic> {
  List<dynamic> _baseList = [];
  List<_PropertyValueStorage> _storage = [];

  _RepeatedPropertyValue(
      final String name, final Property property, final SchemaRegistry registry)
      : super._(name, property, registry);

  dynamic operator [](int i) => _baseList[i];
  int get length => _baseList.length;
  void set length(int l) {
    _baseList.length = l;
  }

  void operator []=(int i, dynamic value) {
    if (!property.validateValue(value)) {
      throw new ArgumentError("Cannot use a value of type ${value.runtimeType} "
          "to set a property $propertyName of type ${property.type}");
    }

    _baseList[i] = value;
  }

  dynamic get value => this;

  @override
  bool hasValue() => this.length > 0;

  void set(dynamic value) {
    if (value is! Iterable) {
      throw new ArgumentError("Cannot use a value of type ${value.runtimeType} "
          "to set a property $propertyName of type List<${property.type}>");
    }

    clear();
    addAll(value);
  }

  void save(final GraphMutator mutator, final Node parentNode,
      final Map<Entity, Node> entityNodeMap) {
    // Delete any list values that we don't need to store the current
    // values (because we now have fewer than we did).
    if (_baseList.length < _storage.length) {
      final List<_PropertyValueStorage> deleteEdges =
          _storage.sublist(_baseList.length);
      deleteEdges.forEach((final _PropertyValueStorage valueStorage) {
        valueStorage.delete(mutator);
      });
    }

    _storage.length = _baseList.length;
    for (int i = 0; i < _baseList.length; ++i) {
      if (_storage[i] == null) {
        _storage[i] = new _PropertyValueStorage(
            [propertyName, '$_indexLabelPrefix$i'], property, registry);
      }

      _storage[i].save(mutator, _baseList[i], parentNode, entityNodeMap);
    }
  }

  void delete(final GraphMutator mutator) {
    _storage.forEach((final _PropertyValueStorage valueStorage) {
      valueStorage.delete(mutator);
    });

    _storage = [];
  }

  void load(final List<Edge> edges, final Map<Node, Entity> nodeEntityMap) {
    _baseList.length = edges.length;
    _storage.length = edges.length;

    edges.forEach((final Edge edge) {
      final String indexLabel = edge.labels.firstWhere(
          (String s) => s.startsWith(_indexLabelPrefix),
          orElse: () => null);
      assert(indexLabel != null);

      int index = int.parse(indexLabel.substring(_indexLabelPrefix.length));
      _storage[index] =
          new _PropertyValueStorage(edge.labels, property, registry);
      _baseList[index] = _storage[index].load([edge], nodeEntityMap);
    });
  }

  @override
  List<dynamic> toJson() => map((dynamic value) => property.encodeJson(value))
      .toList(growable: false);

  @override
  void loadJson(value) {
    if (!(value is List)) {
      value = [value];
    }
    if (property.isBuiltin) {
      set(value.map((v) => property.decode(v)));
    } else {
      set(value.map((v) => new Entity.fromJsonWithSchemas(v, [property.type])));
    }
  }
}

class _PropertyValueStorage {
  final Property _property;
  final List<String> _edgeLabels;
  final SchemaRegistry _registry;
  Edge _edge;

  _PropertyValueStorage(this._edgeLabels, this._property, this._registry);

  void save(final GraphMutator mutator, dynamic value, final Node parentNode,
      final Map<Entity, Node> entityNodeMap) {
    // Otherwise we handle saving ourselves.
    Node valueNode;
    if (_edge == null) {
      // Short-circuit in the case of no value.
      if (value == null) return;

      // We have to treat builtin values differently than Entity values.
      if (_property.isBuiltin) {
        valueNode = mutator.addNode();
      } else {
        assert(value is Entity);
        // If necessary, we recursively save entities so that they are assigned
        // a Node that we can reference.
        if (!entityNodeMap.containsKey(value)) {
          value.save(mutator, entityNodeMap);
        }
        valueNode = entityNodeMap[value];
        assert(valueNode != null);
      }

      _edge = mutator.addEdge(parentNode.id, _edgeLabels, valueNode.id);
    } else {
      if (value == null) {
        // We no longer have a value, but we used to (since _edge != null). We
        // have to delete ourselves.
        delete(mutator);
        _edge = null;
        return;
      }

      valueNode = _edge.target;
    }

    if (_property.isBuiltin) {
      mutator.setValue(valueNode.id, _property.type, _property.encode(value));
    }
  }

  dynamic load(final List<Edge> edges, Map<Node, Entity> nodeEntityMap) {
    _edge = null;
    if (edges.isEmpty) return null;

    // Let's try and load a value from the edge.
    _edge = edges.first;
    final Node valueNode = _edge.target;

    if (_property.isBuiltin) {
      return _property.decode(valueNode.getValue(_property.type));
    } else {
      if (nodeEntityMap.containsKey(valueNode)) {
        return nodeEntityMap[valueNode];
      }
      return new Entity._fromNode(valueNode, _registry, nodeEntityMap);
    }
  }

  void delete(final GraphMutator mutator) {
    if (_edge == null) return;

    mutator.removeEdge(_edge.id);
    if (_property.isBuiltin) mutator.removeNode(_edge.target.id);
  }
}

/// A node with a node value of this type is the root of an [Entity] described
/// by one or more [Schema]s. The node value is a JSON encoded list of the names
/// of these Schemas. The Schemas are registered under these names in a
/// [SchemaRegistry] held in code.
const String _typesLabel = "internal:entityType";

void _saveTypesToNode(
    final GraphMutator mutator, final Node node, List<String> types) {
  mutator.setValue(node.id, _typesLabel,
      new Uint8List.fromList(JSON.encode(types).codeUnits));
}

List<String> _loadTypesFromNode(final Node node) {
  final typesValue = node.getValue(_typesLabel);
  if (typesValue == null) {
    return [];
  }
  final String jsonTypes = new String.fromCharCodes(typesValue);
  return JSON.decode(jsonTypes);
}

/// A node with a node value of this type stores a JSON representation of an
/// [Entity]'s [_Metadata].
const String _metadataLabel = "internal:entityMetadata";

void _saveMetadataToNode(
    final GraphMutator mutator, final Node node, _Metadata metadata) {
  mutator.setValue(node.id, _metadataLabel,
      new Uint8List.fromList(metadata.toJsonString().codeUnits));
}

_Metadata _loadMetadataFromNode(final Node node) {
  final metadataValue = node.getValue(_metadataLabel);
  if (metadataValue == null) {
    return new _Metadata();
  }
  final String metadata = new String.fromCharCodes(metadataValue);
  return new _Metadata.fromJsonString(metadata);
}
