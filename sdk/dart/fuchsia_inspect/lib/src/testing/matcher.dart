// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show utf8;
import 'dart:math' show min;
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:fuchsia_inspect/src/vmo/block.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_fields.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';
import 'package:matcher/matcher.dart';

abstract class _HasErrors {
  List<String> get errors;
  void resetErrors();
}

/// A matcher for testing the structure of Inspect data written to a VMO.
///
/// This matcher aggregates errors from attempting to access or match
/// values stored in the VMO. The set of aggregated errors may be taken
/// and reset using the |errors| getter for the purposes of unit testing an
/// Inspect hierarchy.
///
/// Note that this class is not optimized for efficiency, and should only
/// be used in tests.
class VmoMatcher implements _HasErrors {
  final VmoHolder _holder;
  List<String> _errors = [];

  /// Create a new matcher that matches against the given VmoHolder.
  VmoMatcher(this._holder);

  /// Gets the list of errors.
  @override
  List<String> get errors => _errors;

  /// Resets the recorded errors from this VmoMatcher.
  @override
  void resetErrors() {
    _errors = [];
  }

  /// Retrieve the root node matcher, which can be used to match against
  /// nested properties and children.
  NodeMatcher node() {
    return NodeMatcher._valid(this, 0);
  }

  // Internal method to check if the given index is valid and can be
  // safely read as a block.
  bool _specifiesValidBlock(int index) {
    if (index * bytesPerIndex + 16 > _holder.size) {
      // Entire block header doesn't fit into the VMO.
      return false;
    }
    var blockSize = Block.read(_holder, index).size;

    // Ensure the entire block as specified fits into the VMO.
    return index * bytesPerIndex + blockSize <= _holder.size;
  }

  // Internal method to aggregate errors.
  void _addError(String s) {
    _errors.add(s);
  }

  // Internal method to find the named child of a given parent index.
  int _findNamedValue(String name, int parentIndex) {
    for (int readPosBytes = 0; readPosBytes < _holder.size;) {
      var index = readPosBytes ~/ bytesPerIndex;
      var block = Block.read(_holder, index);
      switch (block.type) {
        case BlockType.nodeValue:
        case BlockType.bufferValue:
        case BlockType.intValue:
        case BlockType.doubleValue:
        case BlockType.boolValue:
          if (block.parentIndex == parentIndex &&
              _nameForBlock(block) == name) {
            return index;
          }
          break;
        default:
          break;
      }
      readPosBytes += block.size;
    }

    return 0;
  }

  // Internal method to find and parse the name for a block.
  String? _nameForBlock(Block block) {
    if (!_specifiesValidBlock(block.nameIndex)) {
      return null;
    }

    var nameBlock = Block.read(_holder, block.nameIndex);
    return _utf8ToString(nameBlock.nameUtf8);
  }

  ByteData _extentsToByteData(Block property) {
    if (property.bufferExtentIndex == 0) {
      return ByteData(0);
    }
    var payloadLength = property.bufferTotalLength;
    var bytes = Uint8List(payloadLength);
    int amountCopied = 0;
    for (Block extentBlock = Block.read(_holder, property.bufferExtentIndex);;
        extentBlock = Block.read(property.vmo, extentBlock.nextExtent)) {
      int copyEnd =
          min(payloadLength, amountCopied + extentBlock.payloadSpaceBytes);
      bytes.setRange(
          amountCopied, copyEnd, extentBlock.payloadBytes.buffer.asUint8List());
      amountCopied += copyEnd - amountCopied;
      if (extentBlock.nextExtent == 0) {
        break;
      }
    }
    return bytes.buffer.asByteData();
  }

  String _extentsToString(Block property) {
    return _utf8ToString(_extentsToByteData(property));
  }
}

/// Matcher for a particular Node in the VMO.
///
/// NodeMatchers may be valid or invalid. A valid NodeMatcher refers to
/// an actual node stored in the VMO, while an invalid NodeMatcher represents
/// a node that could not be found. The creation of an invalid NodeMatcher
/// records an error in the top-level matcher, and operations on it have
/// no effect.
class NodeMatcher implements _HasErrors {
  final VmoMatcher _parent;
  final int _index;
  final bool _valid;

  /// Creates a valid NodeMatcher with the given index.
  NodeMatcher._valid(this._parent, this._index) : _valid = true;

  /// Creates an invalid NodeMatcher.
  NodeMatcher._invalid(this._parent)
      : _valid = false,
        _index = 0;

  /// Get a NodeMatcher for the node at the given path below this one.
  ///
  /// If any step of the path could not be found or is not a node,
  /// an error is recorded and an invalid NodeMatcher is returned.
  NodeMatcher at(List<String> path) {
    if (!_valid) {
      // No error, it was already recorded by the creation of this.
      return NodeMatcher._invalid(_parent);
    }

    int curIndex = _index;
    for (var p in path) {
      int next = _parent._findNamedValue(p, curIndex);
      if (next == 0) {
        _parent._addError('Cannot find node $p in $path');
        return NodeMatcher._invalid(_parent);
      } else if (Block.read(_parent._holder, next).type !=
          BlockType.nodeValue) {
        _parent._addError('Value $p in $path found, but it is not a node');
        return NodeMatcher._invalid(_parent);
      }

      curIndex = next;
    }
    return NodeMatcher._valid(_parent, curIndex);
  }

  /// Asserts that this Node does not have the named child.
  void missingChild(String name) {
    if (!_valid) {
      return;
    }

    int next = _parent._findNamedValue(name, _index);
    if (next != 0) {
      _parent._addError('Found $name which was expected to be missing');
    }
  }

  /// Get a PropertyMatcher for a property on this node.
  ///
  /// If the property cannot be found or is not a value type, an invalid
  /// PropertyMatcher is returned.
  PropertyMatcher property(String name) {
    if (!_valid) {
      // No error, it would have been added creating the node itself.
      return PropertyMatcher._invalid(_parent);
    }

    int valueIndex = _parent._findNamedValue(name, _index);
    if (valueIndex == 0) {
      _parent._addError('Cannot find property $name');
      return PropertyMatcher._invalid(_parent);
    } else if ([
      BlockType.nodeValue,
      BlockType.tombstone,
      BlockType.nameUtf8,
      BlockType.extent
    ].contains(Block.read(_parent._holder, valueIndex).type)) {
      _parent._addError('Value $name found, but it is not a property type');
      return PropertyMatcher._invalid(_parent);
    }

    return PropertyMatcher._valid(_parent, valueIndex);
  }

  /// Checks that a property of this node equals the given value.
  PropertyMatcher propertyEquals(String name, dynamic val) {
    return property(name)..equals(val);
  }

  /// Checks that a property of this node exists but does not equal the given value.
  PropertyMatcher propertyNotEquals(String name, dynamic val) {
    return property(name)..notEquals(val);
  }

  /// Gets the list of errors.
  @override
  List<String> get errors => _parent._errors;

  /// Resets the recorded errors from the parent matcher.
  @override
  void resetErrors() {
    _parent.resetErrors();
  }
}

/// Matcher for a particular Property in the VMO.
///
/// PropertyMatchers may be valid or invalid, with the semantics of
/// NodeMatcher.
class PropertyMatcher implements _HasErrors {
  final VmoMatcher _parent;
  final int _index;

  // Create a valid PropertyMatcher for the given index.
  PropertyMatcher._valid(this._parent, this._index);

  // Create an invalid PropertyMatcher.
  PropertyMatcher._invalid(this._parent) : _index = 0;

  // Internal getter to check if this matcher is valid.
  bool get _valid => _index != 0; // Properties cannot ever be at index 0.

  void _internalEquals(dynamic val, bool invert) {
    if (!_valid) {
      return;
    }

    var negation = invert ? 'not ' : '';
    // ignore: avoid_positional_boolean_parameters
    bool maybeNegate(bool val) => (invert ? !val : val);

    var block = Block.read(_parent._holder, _index);
    if (val is int) {
      if (block.type != BlockType.intValue) {
        _parent
            ._addError('Expected int ($val), found ${block.type.toString()}');
      } else if (maybeNegate(block.intValue != val)) {
        _parent._addError(
            'Expected ${negation}value $val, found ${block.intValue}');
      }
    } else if (val is double) {
      if (block.type != BlockType.doubleValue) {
        _parent._addError(
            'Expected double ($val), found ${block.type.toString()}');
      } else if (maybeNegate(block.doubleValue != val)) {
        _parent._addError(
            'Expected ${negation}value $val, found ${block.doubleValue}');
      }
    } else if (val is bool) {
      if (block.type != BlockType.boolValue) {
        _parent
            ._addError('Expected bool ($val), found ${block.type.toString()}');
      }
    } else if (val is String) {
      if (block.type != BlockType.bufferValue) {
        _parent._addError(
            'Expected string ($val), found ${block.type.toString()}');
      } else if (block.bufferFlags != propertyUtf8Flag) {
        _parent._addError('Expected UTF-8 string, found binary');
      } else {
        var storedValue = _parent._extentsToString(block);
        if (maybeNegate(storedValue != val)) {
          _parent
              ._addError('Expected ${negation}value $val, found $storedValue');
        }
      }
    } else if (val is Uint8List) {
      if (block.type != BlockType.bufferValue) {
        _parent._addError(
            'Expected bytes (${val.join(", ")}), found ${block.type.toString()}');
      } else if (block.bufferFlags == propertyUtf8Flag) {
        _parent._addError('Expected bytes string, found UTF-8');
      } else {
        var storedValue =
            _parent._extentsToByteData(block).buffer.asUint8List();
        if (maybeNegate(!ListEquality().equals(storedValue, val))) {
          _parent._addError(
              'Expected ${negation}value [${val.join(", ")}], found [${storedValue.join(", ")}]');
        }
      }
    } else {
      _parent._addError(
          'Unknown type ${val.runtimeType} passed to matcher. Expected int, double, String, or Uint8List.');
    }
  }

  /// Match that this property equals a value.
  void equals(dynamic val) => _internalEquals(val, false);

  /// Matches that this property exists but does not equal a value.
  void notEquals(dynamic val) => _internalEquals(val, true);

  /// Gets the list of errors.
  @override
  List<String> get errors => _parent._errors;

  /// Resets the recorded errors from the parent matcher.
  @override
  void resetErrors() {
    _parent.resetErrors();
  }
}

class _HasNoErrorsMatcher extends Matcher {
  const _HasNoErrorsMatcher();

  @override
  bool matches(dynamic item, Map<dynamic, dynamic> matchState) {
    if (item is _HasErrors) {
      return item.errors.isEmpty;
    } else {
      return false;
    }
  }

  @override
  Description describe(Description description) {
    return description..add('The Inspect matcher has no recorded errors');
  }

  @override
  Description describeMismatch(dynamic item, Description mismatchDescription,
      Map<dynamic, dynamic> matchState, bool verbose) {
    if (item is _HasErrors) {
      var output = item.errors.join('\n- ');
      mismatchDescription.add('Has errors:\n- $output');
    } else {
      mismatchDescription.add(
          'The item being matched is not a VmoMatcher, NodeMatcher, or PropertyMatcher');
    }
    return mismatchDescription;
  }
}

/// Matcher that asserts a VmoMatcher, NodeMatcher, or PropertyMatcher
/// has no recorded errors.
const Matcher hasNoErrors = _HasNoErrorsMatcher();

String _utf8ToString(ByteData bytes) {
  return utf8.decode(bytes.buffer
      .asUint8ClampedList(bytes.offsetInBytes, bytes.lengthInBytes));
}
