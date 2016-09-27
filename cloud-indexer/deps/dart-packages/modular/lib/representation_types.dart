// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

class UnrecognizedRepresentationLabelException implements Exception {
  final Uri _label;

  const UnrecognizedRepresentationLabelException(this._label);

  @override
  String toString() =>
      'Could not find bindings for representation label \'$_label\'.';
}

class UnrecognizedRuntimeTypeException implements Exception {
  final Type _type;

  const UnrecognizedRuntimeTypeException(this._type);

  @override
  String toString() => 'Could not find bindings for runtime type \'$_type\'.';
}

/// Interface for serialization bindings of a representation type.
abstract class RepresentationBindings<T> {
  String get label;
  T decode(Uint8List data);
  Uint8List encode(T value);
}

/// Represents a single representation value.
class RepresentationValue {
  Uri label;
  Uint8List data;

  RepresentationValue(this.label, this.data);
}

/// Reads and writes [RepresentationValue]s using the registered
/// [RepresentationBindings]s.
class RepresentationBindingsRegistry {
  final Map<Type, RepresentationBindings<dynamic>> _dartTypeToBindings =
      <Type, RepresentationBindings<dynamic>>{};
  final Map<Uri, RepresentationBindings<dynamic>> _labelToBindings =
      <Uri, RepresentationBindings<dynamic>>{};

  void register(Type dartType, RepresentationBindings<dynamic> bindings) {
    _dartTypeToBindings[dartType] = bindings;
    _labelToBindings[Uri.parse(bindings.label)] = bindings;
  }

  dynamic read(final RepresentationValue representationValue) {
    if (!_labelToBindings.containsKey(representationValue.label)) {
      throw new UnrecognizedRepresentationLabelException(
          representationValue.label);
    }

    return _labelToBindings[representationValue.label]
        .decode(representationValue.data);
  }

  RepresentationValue write(dynamic dartValue) {
    if (!_dartTypeToBindings.containsKey(dartValue.runtimeType)) {
      throw new UnrecognizedRuntimeTypeException(dartValue.runtimeType);
    }
    final RepresentationBindings<dynamic> bindings =
        _dartTypeToBindings[dartValue.runtimeType];
    return new RepresentationValue(
        Uri.parse(bindings.label), bindings.encode(dartValue));
  }
}

/// A global bindings registry.
final RepresentationBindingsRegistry bindingsRegistry =
    new RepresentationBindingsRegistry();
