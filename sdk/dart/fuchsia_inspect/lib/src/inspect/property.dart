// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of 'inspect.dart';

/// A key-value pair with a [String] key and a typed value.
abstract class Property<T> {
  /// The VMO index for this property.
  /// @nodoc
  @visibleForTesting
  final int index;

  /// The writer for the underlying VMO.
  ///
  /// Will be set to null if the [Property] has been deleted, could not be
  /// created in the VMO, or was created on a deleted (or never-created) Node.
  /// If so, all actions on this [Property] should be no-ops and not throw.
  VmoWriter? _writer;

  final Node? _parent;
  final String? _name;

  /// Creates a modifiable [Property].
  Property._(this._parent, this._name, this.index, this._writer) {
    if (index == invalidIndex) {
      _writer = null;
    }
  }

  /// Creates a property that never does anything.
  ///
  /// These are returned when calling create-[Property] methods on a deleted
  /// [Node].
  Property.deleted()
      : _writer = null,
        _name = null,
        _parent = null,
        index = invalidIndex;

  /// Returns true only if this [Property] is present in underlying storage.
  bool get valid => _writer != null;

  /// Sets the value of this [Property].
  void setValue(T value);

  /// Deletes this [Property] from underlying storage.
  /// Calls on a deleted [Property] have no effect and do not result in an
  /// error.
  void delete() {
    _delete();
  }

  void _delete({bool deletedByParent = false}) {
    _writer?.deleteEntity(index);
    _writer = null;
    if (!deletedByParent) {
      _parent!._forgetProperty(_name);
    }
  }
}

/// Sets value on properties which store a byte-vector.
mixin BytesProperty<T> on Property<T> {
  @override
  void setValue(T value) {
    _writer?.setBufferProperty(index, value);
  }
}

/// Operations on "Metric" type properties - those which store a number.
mixin Arithmetic<T extends num?> on Property<T> {
  /// Adds [delta] to the value of this metric.
  void add(T delta) {
    _writer?.addMetric(index, delta);
  }

  /// Subtracts [delta] from the value of this metric.
  void subtract(T delta) {
    _writer?.subMetric(index, delta);
  }

  @override
  void setValue(T value) {
    _writer?.setMetric(index, value);
  }
}

/// A [Property] holding an [int].
///
/// Only [Node.intProperty()] can create this object.
class IntProperty extends Property<int> with Arithmetic<int> {
  IntProperty._(String name, Node parent, VmoWriter writer)
      : super._(
            parent, name, writer.createMetric(parent.index, name, 0), writer);

  /// Creates an [IntProperty] that does nothing.
  IntProperty.deleted() : super.deleted();
}

/// A [Property] holding a [bool].
///
/// Only [Node.boolProperty()] can create this object.
class BoolProperty extends Property<bool> {
  BoolProperty._(String name, Node parent, VmoWriter writer)
      : super._(
            parent, name, writer.createBool(parent.index, name, false), writer);

  @override
  void setValue(bool value) {
    _writer?.setBool(index, value);
  }

  /// Creates a [BoolProperty] that does nothing.
  BoolProperty._deleted() : super.deleted();
}

/// A [Property] holding a [double].
///
/// Only [Node.doubleProperty()] can create this object.
class DoubleProperty extends Property<double> with Arithmetic<double> {
  DoubleProperty._(String name, Node parent, VmoWriter writer)
      : super._(
            parent, name, writer.createMetric(parent.index, name, 0.0), writer);

  /// Creates a [DoubleProperty] that does nothing.
  DoubleProperty.deleted() : super.deleted();
}

/// A [Property] holding a [String].
///
/// Only [Node.stringProperty()] can create this object.
class StringProperty extends Property<String> with BytesProperty<String> {
  StringProperty._(String name, Node parent, VmoWriter writer)
      : super._(parent, name, writer.createBufferProperty(parent.index, name),
            writer);

  /// Creates a [StringProperty] that does nothing.
  StringProperty.deleted() : super.deleted();
}

/// A [Property] holding a [ByteData].
///
/// Only [Node.byteDataProperty()] can create this object.
class ByteDataProperty extends Property<ByteData> with BytesProperty<ByteData> {
  ByteDataProperty._(String name, Node parent, VmoWriter writer)
      : super._(parent, name, writer.createBufferProperty(parent.index, name),
            writer);

  /// Creates a [ByteDataProperty] that does nothing.
  ByteDataProperty.deleted() : super.deleted();
}
