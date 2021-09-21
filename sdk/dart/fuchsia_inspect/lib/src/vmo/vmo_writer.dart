// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as

import 'dart:io';
import 'dart:math' show min;
import 'dart:typed_data';

import 'package:fuchsia_vfs/vfs.dart';
import 'package:zircon/zircon.dart';

import '../testing/util.dart';
import 'block.dart';
import 'heap.dart';
import 'little_big_slab.dart';
import 'util.dart';
import 'vmo_fields.dart';
import 'vmo_holder.dart';

/// Index 0 will never be allocated, so it's the designated 'invalid' value.
const int invalidIndex = 0;

/// An Inspect-format VMO with accessors.
///
/// This writes Values (Nodes, Metrics, and Properties) to
/// a VMO, modifies them, and deletes them.
///
/// Values are referred to by integers, which are returned upon
/// creation and are passed back to this API to modify or delete the Value.
/// You can refer to this integer as an "index".
/// An index of 0 returned from a Value creation operation indicates the
/// operation failed. Other indexes are opaque and indicate success.
///
/// Warning: Names are limited to 24 bytes, and are UTF-8 encoded. In the case
/// of non-ASCII characters, this may result in a malformed string since the
/// last multi-byte character may be truncated.
class VmoWriter {
  late Heap _heap;

  final VmoHolder _vmo;

  late Block _headerBlock;

  /// Constructor.
  VmoWriter(this._vmo, Function heapFactory) {
    _vmo.beginWork();
    _headerBlock = Block.create(_vmo, headerIndex)..becomeHeader();
    _heap = heapFactory(_vmo);
    _vmo.commit();
  }

  /// Creates a [VmoWriter] with a VMO of size [size].
  ///
  /// Note: this method is safe to call on non-fuchsia platforms since it will
  /// create a [FakeVmoHolder] if we aren't running on Fuchsia. This is useful
  /// for not needing to refactor code that needs to run in host side tests.
  factory VmoWriter.withSize(int size) => VmoWriter.withVmo(
      Platform.isFuchsia ? VmoHolder(size) : FakeVmoHolder(size) as VmoHolder);

  /// Function used for creating the heap by default.
  static const Function _heapCreate = LittleBigSlab.create;

  /// Creates a [VmoWriter] with a given VMO.
  factory VmoWriter.withVmo(VmoHolder vmo) => VmoWriter(vmo, _heapCreate);

  /// Gets the top Node of the Inspect tree (always set to index 0, which is the header).
  int get rootNode => 0;

  /// The underlying VMO
  Vmo? get vmo => _vmo.vmo;

  /// The read-only node of the VMO.
  VmoFile get vmoNode =>
      VmoFile.readOnly(_vmo.vmo!, VmoSharingMode.shareDuplicate);

  /// Creates and writes a Node block
  int createNode(int parent, String? name) {
    _beginWork();
    try {
      var node = _createValue(parent, name);
      if (node == null) {
        return invalidIndex;
      }
      node.becomeNode();
      return node.index;
    } finally {
      _commit();
    }
  }

  /// Adds a named BufferValue to a Node.
  int createBufferProperty(int parent, String name) {
    _beginWork();
    try {
      var property = _createValue(parent, name);
      if (property == null) {
        return invalidIndex;
      }
      property.becomeBuffer();
      return property.index;
    } finally {
      _commit();
    }
  }

  /// Sets a Buffer's value from [String] or [ByteData].
  ///
  /// First frees any existing value, then tries to allocate space for the new
  /// value. If the new allocation fails, the previous
  /// value will be cleared and the BufferValue will be empty.
  ///
  /// Throws ArgumentError if [value] is not [String] or [ByteData].
  void setBufferProperty(int propertyIndex, dynamic value) {
    _beginWork();
    try {
      if (!(value is String || value is ByteData)) {
        throw ArgumentError('Property value must be String or ByteData.');
      }

      var property = Block.read(_vmo, propertyIndex);
      _freeExtents(property.bufferExtentIndex);
      ByteData? valueToWrite;
      if (value is String) {
        valueToWrite = toByteData(value);
        property.bufferFlags = propertyUtf8Flag;
      } else if (value is ByteData) {
        valueToWrite = value;
        property.bufferFlags = propertyBinaryFlag;
      }

      if (valueToWrite == null || valueToWrite.lengthInBytes == 0) {
        property.bufferExtentIndex = invalidIndex;
      } else {
        property.bufferExtentIndex =
            _allocateExtents(valueToWrite.lengthInBytes);
        if (property.bufferExtentIndex == invalidIndex) {
          property.bufferTotalLength = 0;
        } else {
          _copyToExtents(property.bufferExtentIndex, valueToWrite);
          property.bufferTotalLength = valueToWrite.lengthInBytes;
        }
      }
    } finally {
      _commit();
    }
  }

  /// Creates and assigns value.
  int createMetric<T extends num>(int parent, String name, T value) {
    _beginWork();
    try {
      Block? metric = _createValue(parent, name);
      if (metric == null) {
        return invalidIndex;
      }
      if (value is double) {
        metric.becomeDoubleMetric(value.toDouble());
      } else {
        metric.becomeIntMetric(value.toInt());
      }
      return metric.index;
    } finally {
      _commit();
    }
  }

  /// Creates and assigns bool.
  int createBool<T extends bool>(int parent, String name, T value) {
    _beginWork();
    try {
      Block? metric = _createValue(parent, name);
      if (metric == null) {
        return invalidIndex;
      }
      metric.becomeBoolMetric(value);
      return metric.index;
    } finally {
      _commit();
    }
  }

  /// Sets a bool property. This is a clone of the setMetric function specifically made for bools since they are a type of metric, but not numerics.
  void setBool<T extends bool>(int metricIndex, T value) {
    _beginWork();
    try {
      Block.read(_vmo, metricIndex).boolValue = value;
    } finally {
      _commit();
    }
  }

  /// Set the metric's value.
  void setMetric<T extends num?>(int metricIndex, T value) {
    _beginWork();
    try {
      var metric = Block.read(_vmo, metricIndex);
      if (value is double) {
        metric.doubleValue = value.toDouble();
      } else {
        if (value != null) {
          metric.intValue = value.toInt();
        }
      }
    } finally {
      _commit();
    }
  }

  /// Adds to existing value.
  void addMetric<T extends num?>(int metricIndex, T value) {
    _beginWork();
    try {
      var metric = Block.read(_vmo, metricIndex);
      if (T is double || metric.type == BlockType.doubleValue) {
        metric.doubleValue += value as double;
      } else {
        metric.intValue += value as int;
      }
    } finally {
      _commit();
    }
  }

  /// Subtracts from existing value.
  void subMetric<T extends num?>(int metricIndex, T value) {
    _beginWork();
    try {
      var metric = Block.read(_vmo, metricIndex);
      if (T is double || metric.type == BlockType.doubleValue) {
        metric.doubleValue -= value as double;
      } else {
        metric.intValue -= value as int;
      }
    } finally {
      _commit();
    }
  }

  // Creates a new *_VALUE node inside the tree.
  Block? _createValue(int parent, String? name) {
    var block = _heap.allocateBlock(32);
    if (block == null) {
      return null;
    }
    var nameBlock = _heap.allocateBlock(name!.length, required: true);
    if (nameBlock == null) {
      _heap.freeBlock(block);
      return null;
    }
    nameBlock.becomeName(name);
    block.becomeValue(parentIndex: parent, nameIndex: nameBlock.index);
    if (parent != 0) {
      // Only increment child count if the parent is not the special header block.
      Block.read(_vmo, parent).childCount += 1;
    }
    return block;
  }

  /// Deletes a *_VALUE block (Node, Buffer, Metric).
  ///
  /// This always unparents the block and frees its NAME block.
  ///
  /// Special cases:
  ///  - If index < heapStartIndex, throw.
  ///  - If block is a Node with children, make it a [BlockType.tombstone]
  ///   instead of freeing. It will be deleted later, when its
  ///   last child is deleted and unparented.
  ///  - If block is a [BlockType.bufferValue], free its extents as well.
  void deleteEntity(int index) {
    if (index < heapStartIndex) {
      throw Exception('Invalid index {nodeIndex}');
    }
    _beginWork();
    try {
      Block value = Block.read(_vmo, index);
      _unparent(value);
      _heap.freeBlock(Block.read(_vmo, value.nameIndex));
      if (value.type == BlockType.nodeValue && value.childCount != 0) {
        value.becomeTombstone();
      } else {
        if (value.type == BlockType.bufferValue) {
          _freeExtents(value.bufferExtentIndex);
        }
        _heap.freeBlock(value);
      }
    } finally {
      _commit();
    }
  }

  /// Walks a chain of EXTENT blocks, freeing them.
  ///
  /// Passing in [invalidIndex] is legal and a NOP.
  void _freeExtents(int firstExtent) {
    int nextIndex = firstExtent;
    while (nextIndex != invalidIndex) {
      var extent = Block.read(_vmo, nextIndex);
      nextIndex = extent.nextExtent;
      _heap.freeBlock(extent);
    }
  }

  /// Tries to allocate enough extents to hold [size] bytes.
  ///
  /// If it finds enough, it returns the index of first EXTENT block in chain.
  /// Returns [invalidIndex] (and frees whatever it's grabbed) if it can't
  /// allocate all it needs.
  int _allocateExtents(int size) {
    int nextIndex = invalidIndex;
    int sizeRemaining = size;
    while (sizeRemaining > 0) {
      var extent = _heap.allocateBlock(sizeRemaining);
      if (extent == null) {
        _freeExtents(nextIndex);
        return 0;
      }
      extent.becomeExtent(nextIndex);
      nextIndex = extent.index;
      sizeRemaining -= extent.payloadSpaceBytes;
    }
    return nextIndex;
  }

  // Copies bytes from value to list of extents.
  //
  // Throws StateError if the extents run out before the data does.
  void _copyToExtents(int firstExtent, ByteData value) {
    int valueOffset = 0;
    int nextExtent = firstExtent;
    // ignore: literal_only_boolean_expressions
    while (valueOffset != value.lengthInBytes) {
      if (nextExtent == 0) {
        throw StateError('Not enough extents to hold the data');
      }
      Block extent = Block.read(_vmo, nextExtent);
      int amountToWrite =
          min(value.lengthInBytes - valueOffset, extent.payloadSpaceBytes);
      _vmo.write(
          nextExtent * 16 + 8,
          value.buffer
              .asByteData(valueOffset + value.offsetInBytes, amountToWrite));
      valueOffset += amountToWrite;
      nextExtent = extent.nextExtent;
    }
  }

  // Decrement the parent's 'childCount' count. Don't change the parent's type.
  // If the parent is a TOMBSTONE and has no children then free it.
  // TOMBSTONES have no parent, so there's no recursion.
  void _unparent(Block value) {
    if (value.parentIndex == 0) {
      // Never delete the header node.
      return;
    }

    var parent = Block.read(_vmo, value.parentIndex);
    if (--parent.childCount == 0 && parent.type == BlockType.tombstone) {
      _heap.freeBlock(parent);
    }
  }

  // Start manipulating the VMO contents.
  void _beginWork() {
    _headerBlock.lock();
    _vmo.beginWork(); // TODO(fxbug.dev/4487): Maybe remove once VMO is efficient.
  }

  // Publish the manipulated VMO contents.
  void _commit() {
    _vmo.commit();
    _headerBlock.unlock();
  }
}
