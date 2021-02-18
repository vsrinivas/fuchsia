// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as

part of 'inspect.dart';

/// The index of the implicit root inspect node. Nodes with a parentIndex equal
/// to this value are children of the root node. Since the root node is implicit
/// this index is not a valid index for serialization.
/// @nodoc
const int rootNodeIndex = 0;

const _kHealthMessageName = 'message';
const _kHealthStatusName = 'status';
const _kTimestampName = 'start_timestamp_nanos';

/// A named node in the Inspect tree that can have [Node]s and
/// properties under it.
class Node {
  /// The VMO index of this node.
  /// @nodoc
  @visibleForTesting
  final int index;

  /// The writer for the underlying VMO.
  ///
  /// Will be set to null if the Node has been deleted or could not be
  /// created in the VMO.
  /// If so, all actions on this Node should be no-ops and not throw.
  VmoWriter? _writer;

  final _properties = <String, Property>{};
  final _children = <String, Node>{};
  final Node? _parent;
  final String? _name;

  /// Creates a [Node] with [name] under the [parentIndex].
  ///
  /// Private as an implementation detail to code that understands VMO indices.
  /// Client code that wishes to create [Node]s should use [child].
  Node._(this._parent, this._name, int parentIndex, VmoWriter this._writer)
      : index = _writer.createNode(parentIndex, _name) {
    if (index == invalidIndex) {
      _writer = null;
    }
  }

  /// Wraps the special root node.
  Node._root(VmoWriter this._writer)
      : index = _writer.rootNode,
        _parent = null,
        _name = null;

  /// Creates a Node that never does anything.
  ///
  /// These are returned when calling createChild on a deleted [Node].
  Node.deleted()
      : _writer = null,
        _parent = null,
        _name = null,
        index = invalidIndex;

  /// Returns a child [Node] with [name].
  ///
  /// If a child with [name] already exists and was not deleted, this
  /// method returns it. Otherwise, it creates a new [Node].
  Node? child(String name) {
    if (_writer == null) {
      return Node.deleted();
    }
    if (_children.containsKey(name)) {
      return _children[name];
    }
    return _children[name] = Node._(this, name, index, _writer!);
  }

  /// Returns true only if this node is present in underlying storage.
  bool get valid => _writer != null;

  void _forgetChild(String? name) {
    _children.remove(name);
  }

  void _forgetProperty(String? name) {
    _properties.remove(name);
  }

  /// Deletes this node and any children from underlying storage.
  ///
  /// After a node has been deleted, all calls on it and its children have
  /// no effect and do not result in an error. Calls on a deleted node that
  /// return a Node or property return an already-deleted object.
  void delete() {
    _delete();
  }

  void _delete({bool deletedByParent = false}) {
    if (_writer == null) {
      return;
    }
    _properties
      ..forEach((_, property) => property._delete(deletedByParent: true))
      ..clear();
    _children
      ..forEach((_, node) => node._delete(deletedByParent: true))
      ..clear();

    if (!deletedByParent) {
      _parent!._forgetChild(_name);
    }
    _writer!.deleteEntity(index);
    _writer = null;
  }

  /// Returns a [StringProperty] with [name] on this node.
  ///
  /// If a [StringProperty] with [name] already exists and is not deleted,
  /// this method returns it.
  ///
  /// Otherwise, it creates a new property initialized to the empty string.
  ///
  /// Throws [InspectStateError] if a non-deleted property with [name] already
  /// exists but it is not a [StringProperty].
  StringProperty? stringProperty(String name) {
    if (_writer == null) {
      return StringProperty.deleted();
    }
    if (_properties.containsKey(name)) {
      if (_properties[name] is! StringProperty) {
        throw InspectStateError("Can't create StringProperty named $name;"
            ' a different type exists.');
      }
      return _properties[name] as StringProperty?;
    }
    return _properties[name] = StringProperty._(name, this, _writer!);
  }

  /// Returns a [ByteDataProperty] with [name] on this node.
  ///
  /// If a [ByteDataProperty] with [name] already exists and is not deleted,
  /// this method returns it.
  ///
  /// Otherwise, it creates a new property initialized to the empty
  /// byte data container.
  ///
  /// Throws [InspectStateError] if a non-deleted property with [name] already exists
  /// but it is not a [ByteDataProperty].
  ByteDataProperty? byteDataProperty(String name) {
    if (_writer == null) {
      return ByteDataProperty.deleted();
    }
    if (_properties.containsKey(name)) {
      if (_properties[name] is! ByteDataProperty) {
        throw InspectStateError("Can't create ByteDataProperty named $name;"
            ' a different type exists.');
      }
      return _properties[name] as ByteDataProperty?;
    }
    return _properties[name] = ByteDataProperty._(name, this, _writer!);
  }

  /// Returns an [IntProperty] with [name] on this node.
  ///
  /// If an [IntProperty] with [name] already exists and is not
  /// deleted, this method returns it.
  ///
  /// Otherwise, it creates a new property initialized to 0.
  ///
  /// Throws [InspectStateError] if a non-deleted property with [name]
  /// already exists but it is not an [IntProperty].
  IntProperty? intProperty(String name) {
    if (_writer == null) {
      return IntProperty.deleted();
    }
    if (_properties.containsKey(name)) {
      if (_properties[name] is! IntProperty) {
        throw InspectStateError(
            "Can't create IntProperty named $name; a different type exists.");
      }
      return _properties[name] as IntProperty?;
    }
    return _properties[name] = IntProperty._(name, this, _writer!);
  }

  /// Returns an [BoolProperty] with [name] on this node.
  ///
  /// If an [BoolProperty] with [name] already exists and is not
  /// deleted, this method returns it.
  ///
  /// Otherwise, it creates a new property initialized to false.
  ///
  /// Throws [InspectStateError] if a non-deleted property with [name]
  /// already exists but it is not a [BoolProperty].
  BoolProperty? boolProperty(String name) {
    if (_writer == null) {
      return BoolProperty._deleted();
    }
    if (_properties.containsKey(name)) {
      if (_properties[name] is! BoolProperty) {
        throw InspectStateError(
            "Can't create BoolProperty named $name; a different type exists.");
      }
      return _properties[name] as BoolProperty?;
    }
    return _properties[name] = BoolProperty._(name, this, _writer!);
  }

  /// Returns a [DoubleProperty] with [name] on this node.
  ///
  /// If a [DoubleProperty] with [name] already exists and is not
  /// deleted, this method returns it.
  ///
  /// Otherwise, it creates a new property initialized to 0.0.
  ///
  /// Throws [InspectStateError] if a non-deleted property with [name]
  /// already exists but it is not a [DoubleProperty].
  DoubleProperty? doubleProperty(String name) {
    if (_writer == null) {
      return DoubleProperty.deleted();
    }
    if (_properties.containsKey(name)) {
      if (_properties[name] is! DoubleProperty) {
        throw InspectStateError("Can't create DoubleProperty named $name;"
            ' a different type exists.');
      }
      return _properties[name] as DoubleProperty?;
    }
    return _properties[name] = DoubleProperty._(name, this, _writer!);
  }
}

/// RootNode wraps the root node of the VMO.
///
/// The root node has special behavior: Delete is a NOP.
///
/// This class should be hidden from the public API.
/// @nodoc
class RootNode extends Node {
  /// Creates a Node wrapping the root of the Inspect hierarchy.
  RootNode(VmoWriter writer) : super._root(writer);

  /// Deletes of the root are NOPs.
  @override
  void delete() {}
}

enum _Status {
  startingUp,
  ok,
  unhealthy,
}

/// Contains subsystem health information.
class HealthNode {
  _Status? _status;
  Node? _node;

  /// Creates a new health node on the given node.
  HealthNode(Node? node)
      : this.withTimeNanosForTest(node, () {
          // Nanosecond resolution is not available with [DateTime], so we
          // manufacture it.
          return DateTime.now().microsecondsSinceEpoch * 1000;
        });

  /// Creates a new health node on the given node, using the supplied function to retrieve a 64-bit
  /// current timestamp.  For testing ONLY.  Sadly can not be marked as @visibleForTesting as it is
  /// used in the test fixtures in other packages.
  HealthNode.withTimeNanosForTest(Node? node, Function timeNanos) {
    _node = node;
    _node!.intProperty(_kTimestampName)!.setValue(timeNanos());
    _setStatus(_Status.startingUp);
  }

  /// Sets the status of the health node to STARTING_UP.
  void setStartingUp() {
    _setStatus(_Status.startingUp);
  }

  /// Sets the status of the health node to OK.
  void setOk() {
    _setStatus(_Status.ok);
  }

  /// Sets the status of the health node to UNHEALTHY and records the given
  /// `message`.
  void setUnhealthy(String message) {
    _setStatus(_Status.unhealthy, message: message);
  }

  String _statusString() {
    switch (_status) {
      case _Status.startingUp:
        return 'STARTING_UP';
      case _Status.ok:
        return 'OK';
      case _Status.unhealthy:
        return 'UNHEALTHY';
      default:
        return 'UNKOWN';
    }
  }

  void _setStatus(_Status status, {String? message}) {
    _status = status;
    _node!.stringProperty(_kHealthStatusName)!.setValue(_statusString());
    if (message != null) {
      _node!.stringProperty(_kHealthMessageName)!.setValue(message);
    } else {
      _node!.stringProperty(_kHealthMessageName)!.delete();
    }
  }
}
