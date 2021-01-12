// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:meta/meta.dart';

import 'bitfield64.dart';
import 'heap.dart';
import 'util.dart';
import 'vmo_fields.dart';
import 'vmo_holder.dart';

/// Mirrors a single block in the VMO.
///
/// Can be read from VMO and/or initialized and modified by code, then
/// written to VMO if desired.
class Block {
  final _header = Bitfield64();
  final _payloadBits = Bitfield64();

  final VmoHolder _vmo;

  /// Index of the block within the VMO
  final int index;

  /// The VMO this Block lives inside.
  @visibleForTesting
  VmoHolder get vmo => _vmo;

  /// Initializes an empty [BlockType.reserved] block that isn't in the VMO yet.
  Block.create(this._vmo, this.index, {int order = defaultBlockOrder}) {
    _header..write(typeBits, BlockType.reserved.value)..write(orderBits, order);
  }

  /// Create a block with arbitrary type.
  /// @nodoc
  @visibleForTesting
  Block.createWithType(this._vmo, this.index, BlockType type) {
    _header..write(typeBits, type.value)..write(orderBits, defaultBlockOrder);
  }

  /// Initializes by reading the block from the VMO.
  Block.read(this._vmo, this.index) {
    // TODO(fxbug.dev/4487): Validate index. More validating of parameters everywhere.
    _header.value = _vmo.readInt64(_offset);
    _payloadBits.value = _vmo.readInt64(_payloadOffset);
  }

  /// The block's payload as a string of bytes (for [BlockType.nameUtf8] or
  /// [BlockType.extent]).
  /// @nodoc
  @visibleForTesting
  ByteData get payloadBytes =>
      _vmo.read(_payloadOffset, size - headerSizeBytes);

  void _writeHeader() {
    _vmo.writeInt64(_offset, _header.value);
  }

  void _writePayloadBits() {
    _vmo.writeInt64(_payloadOffset, _payloadBits.value);
  }

  void _writeAllBits() {
    _writeHeader();
    _writePayloadBits();
  }

  void _writePayloadBytes(ByteData bytes) {
    _vmo.write(_payloadOffset, bytes);
  }

  /// Initializes (the one and only) [BlockType.header] block.
  void becomeHeader() {
    _header
      ..write(typeBits, BlockType.header.value)
      ..write(orderBits, 0)
      ..write(headerMagicBits, headerMagicNumber)
      ..write(headerVersionBits, headerVersionNumber);
    _payloadBits.value = 0;
    _writeAllBits();
  }

  /// Start a VMO update.
  ///
  /// Only valid for the [BlockType.header] block; otherwise throws
  /// [StateError].
  void lock() {
    _checkType(BlockType.header);
    _checkLocked(false);
    _payloadBits.value++;
    _vmo.writeInt64Direct(_payloadOffset, _payloadBits.value);
  }

  /// Finish a VMO update.
  /// Only valid for the [BlockType.header] block; otherwise throws
  /// [StateError].
  void unlock() {
    _checkType(BlockType.header);
    _checkLocked(true);
    _payloadBits.value++;
    _vmo.writeInt64Direct(_payloadOffset, _payloadBits.value);
  }

  /// Converts a [BlockType.anyValue] block to a [BlockType.nodeValue] block.
  ///
  /// Throws [StateError] if this block wasn't [BlockType.anyValue].
  void becomeNode() {
    _checkType(BlockType.anyValue);
    _header.write(typeBits, BlockType.nodeValue.value);
    _payloadBits.value = 0;
    _writeAllBits();
  }

  /// Child count of [BlockType.nodeValue] or [BlockType.tombstone] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.nodeValue]
  /// or [BlockType.tombstone].
  set childCount(int value) {
    _checkNodeOrTombstone();
    _payloadBits.value = value;
    _writePayloadBits();
  }

  /// Child count of [BlockType.nodeValue] or [BlockType.tombstone] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.nodeValue]
  /// or [BlockType.tombstone].
  int get childCount {
    _checkNodeOrTombstone();
    return _payloadBits.value;
  }

  /// Converts an anyValue block to a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block wasn't [BlockType.anyValue].
  void becomeBuffer() {
    _checkType(BlockType.anyValue);
    _header.write(typeBits, BlockType.bufferValue.value);
    _payloadBits
      ..write(bufferExtentIndexBits, 0)
      ..write(bufferTotalLengthBits, 0)
      ..write(bufferFlagsBits, 0);
    _writeAllBits();
  }

  /// Total Length field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  int get bufferTotalLength {
    _checkType(BlockType.bufferValue);
    return _payloadBits.read(bufferTotalLengthBits);
  }

  /// Total Length field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  set bufferTotalLength(int length) {
    _checkType(BlockType.bufferValue);
    _payloadBits.write(bufferTotalLengthBits, length);
    _writePayloadBits();
  }

  /// Extent Index field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  int get bufferExtentIndex {
    _checkType(BlockType.bufferValue);
    return _payloadBits.read(bufferExtentIndexBits);
  }

  /// Extent Index field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  set bufferExtentIndex(int index) {
    _checkType(BlockType.bufferValue);
    _payloadBits.write(bufferExtentIndexBits, index);
    _writePayloadBits();
  }

  /// Flags field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  int get bufferFlags {
    _checkType(BlockType.bufferValue);
    return _payloadBits.read(bufferFlagsBits);
  }

  /// Flags field of a [BlockType.bufferValue] block.
  ///
  /// Throws [StateError] if this block isn't [BlockType.bufferValue].
  set bufferFlags(int flags) {
    _checkType(BlockType.bufferValue);
    _payloadBits.write(bufferFlagsBits, flags);
    _writePayloadBits();
  }

  /// Converts a [BlockType.nodeValue] block to a [BlockType.tombstone] block.
  ///
  /// Throws [StateError] if this block wasn't [BlockType.nodeValue].
  void becomeTombstone() {
    _checkType(BlockType.nodeValue);
    _header.write(typeBits, BlockType.tombstone.value);
    _writeHeader();
  }

  /// Makes any block [BlockType.free].
  void becomeFree(int next) {
    var orderValue = _header.read(orderBits);
    _header
      ..value = 0
      ..write(orderBits, orderValue)
      ..write(typeBits, BlockType.free.value)
      ..write(nextFreeBits, next);
    _writeHeader();
  }

  /// Converts a [BlockType.free] to a [BlockType.reserved] block.
  ///
  /// Throws [StateError] if this block wasn't [BlockType.nodeValue].
  void becomeReserved() {
    _checkType(BlockType.free);
    _header.write(typeBits, BlockType.reserved.value);
    _writeHeader();
  }

  /// Index of next-free-block.
  ///
  /// Throws [StateError] if block isn't [BlockType.free].
  int get nextFree {
    _checkType(BlockType.free);
    return _header.read(nextFreeBits); // _header.read(nextFreeBits);
  }

  /// Initializes a value-holding block as [BlockType.anyValue].
  ///
  /// Does not write it, because [BlockType.anyValue] is not part of VMO format.
  /// This is a helper function called prior to [becomeNode()],
  /// [becomeBuffer()], and [becomeMetric()].
  ///
  /// Throws [StateError] if block wasn't [BlockType.reserved].
  void becomeValue({required int nameIndex, required int parentIndex}) {
    _checkType(BlockType.reserved);
    _header
      ..write(typeBits, BlockType.anyValue.value)
      ..write(parentIndexBits, parentIndex)
      ..write(nameIndexBits, nameIndex);
    _writeHeader();
  }

  /// The index of the [BlockType.name] block of a *_VALUE block.
  ///
  /// Throws [StateError] if block isn't a value-holding block.
  int get nameIndex {
    _checkIsValue();
    return _header.read(nameIndexBits);
  }

  /// The index of the parent [BlockType.nodeValue] block of a *_VALUE block.
  ///
  /// Throws [StateError] if block isn't a value-holding block.
  int get parentIndex {
    _checkIsValue();
    return _header.read(parentIndexBits);
  }

  /// Converts a [BlockType.anyValue] block to a [BlockType.doubleValue] block,
  /// and sets starting [value].
  ///
  /// Throws [StateError] if block wasn't a [BlockType.anyValue] block.
  void becomeDoubleMetric(double value) {
    _checkType(BlockType.anyValue);
    _header.write(typeBits, BlockType.doubleValue.value);
    _payloadBits.value = _doubleBitsToInt(value);
    _writeAllBits();
  }

  /// Converts a [BlockType.anyValue] block to a [BlockType.intValue] block,
  /// and sets starting [value].
  ///
  /// Throws [StateError] if block wasn't a [BlockType.anyValue] block.
  void becomeIntMetric(int value) {
    _checkType(BlockType.anyValue);
    _header.write(typeBits, BlockType.intValue.value);
    _payloadBits.value = value;
    _writeAllBits();
  }

  /// Converts a [BlockType.anyValue] block to a [BlockType.boolValue] block,
  /// and sets starting [value].
  ///
  /// Throws [StateError] if block wasn't a [BlockType.anyValue] block.
  // ignore: avoid_positional_boolean_parameters
  void becomeBoolMetric(bool value) {
    _checkType(BlockType.anyValue);
    _header.write(typeBits, BlockType.boolValue.value);
    _payloadBits.value = value ? 1 : 0;
    _writeAllBits();
  }

  /// Value payload stored in a [BlockType.boolValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.intValue] block.
  bool get boolValue {
    _checkType(BlockType.boolValue);
    if (_payloadBits.value == 1) {
      return true;
    } else if (_payloadBits.value == 0) {
      return false;
    } else {
      // Should never get here.
      throw StateError(
          'Invalid boolean value $_payloadBits.value (must be 0 or 1)');
    }
  }

  /// Writes bool value payload to a [BlockType.boolValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.boolValue] block.
  set boolValue(bool value) {
    _checkType(BlockType.boolValue);
    _payloadBits.value = value ? 1 : 0;
    _writePayloadBits();
  }

  /// Value payload stored in a [BlockType.intValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.intValue] block.
  int get intValue {
    _checkType(BlockType.intValue);
    return _payloadBits.value;
  }

  /// Writes int value payload to a [BlockType.intValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.intValue] block.
  set intValue(int value) {
    _checkType(BlockType.intValue);
    _payloadBits.value = value;
    _writePayloadBits();
  }

  /// Value payload stored in a [BlockType.doubleValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.doubleValue] block.
  double get doubleValue {
    _checkType(BlockType.doubleValue);
    return _intBitsToDouble(_payloadBits.value);
  }

  /// Write double value payload to a [BlockType.doubleValue] block.
  ///
  /// Throws [StateError] if block isn't a [BlockType.doubleValue] block.
  set doubleValue(double value) {
    _checkType(BlockType.doubleValue);
    _payloadBits.value = _doubleBitsToInt(value);
    _writePayloadBits();
  }

  /// Initializes a [BlockType.name] block.
  ///
  /// Throws [StateError] if block wasn't a [BlockType.reserved] block.
  void becomeName(String name) {
    _checkType(BlockType.reserved);
    var stringBytes = toByteData(name, maxBytes: payloadSpaceBytes);
    _header
      ..write(typeBits, BlockType.nameUtf8.value)
      ..write(nameLengthBits, stringBytes.lengthInBytes);
    _writeHeader();
    _writePayloadBytes(stringBytes);
  }

  /// Gets the utf8 name from a NAME block
  /// @nodoc
  @visibleForTesting
  ByteData get nameUtf8 {
    _checkType(BlockType.nameUtf8);
    return ByteData.view(payloadBytes.buffer, payloadBytes.offsetInBytes,
        _header.read(nameLengthBits));
  }

  /// Adds a [BlockType.reserved] block to the head of a [BlockType.extent]
  /// chain.
  ///
  /// Throws [StateError] if block wasn't a [BlockType.reserved] block.
  void becomeExtent(int nextExtent) {
    _checkType(BlockType.reserved);
    _header
      ..write(typeBits, BlockType.extent.value)
      ..write(nextExtentBits, nextExtent);
    _writeHeader();
  }

  /// Writes the [BlockType.extent]'s data.
  ///
  /// Throws [StateError] if block isn't a [BlockType.extent] block.
  void setExtentPayload(ByteData data) {
    _checkType(BlockType.extent);
    _writePayloadBytes(data);
  }

  /// The next [Block] in a [BlockType.extent] chain.
  ///
  /// Throws [StateError] if block isn't a [BlockType.extent] block.
  int get nextExtent {
    _checkType(BlockType.extent);
    return _header.read(nextExtentBits);
  }

  /// The number of bytes available for payload in this [BlockType.extent]
  /// [Block].
  int get payloadSpaceBytes {
    return size - headerSizeBytes;
  }

  /// The VMO-format-defined type of this [Block].
  BlockType get type => BlockType.values[_header.read(typeBits)];

  /// Size of the [Block] in bytes.
  int get size => 1 << (_header.read(orderBits) + 4);

  /// Verifies this [Block] has the expected type; throws [StateError] if not.
  void _checkType(BlockType blockType) {
    if (type != blockType) {
      throw StateError('Incorrect block type index: $index, size: $size: '
          'expected $blockType, but found $type.');
    }
  }

  /// Throws [StateError] if [type] is not [BlockType.nodeValue] or
  /// [BlockType.tombstone].
  void _checkNodeOrTombstone() {
    if (type != BlockType.nodeValue && type != BlockType.tombstone) {
      throw StateError('Incorrect block type: '
          'expected node or tombstone, but found $type.');
    }
  }

  /// Throws [StateError] if [type] is not a *_VALUE block, i.e.
  /// [BlockType.nodeValue], [BlockType.anyValue], [BlockType.intValue],
  /// [BlockType.doubleValue], or [BlockType.bufferValue].
  void _checkIsValue() {
    if (type != BlockType.anyValue &&
        type != BlockType.nodeValue &&
        type != BlockType.bufferValue &&
        type != BlockType.intValue &&
        type != BlockType.doubleValue &&
        type != BlockType.boolValue) {
      throw StateError('Value block expected; this block is $type.');
    }
  }

  /// This block (verified elsewhere to be the HEADER) "locks" the VMO during
  /// updates: if the payload value is odd, the VMO contents are in flux,
  /// and readers should retry.
  void _checkLocked(bool locked) {
    if ((_payloadBits.value & 1 == 1) != locked) {
      throw StateError('Lock state error; expected locked = $locked.');
    }
  }

  /// Converts 64 [bits] (supplied as int) to the [double] they really are.
  double _intBitsToDouble(int bits) {
    var scratchpad = ByteData(8)..setInt64(0, bits);
    return scratchpad.getFloat64(0);
  }

  /// Converts a [double] [value] to its 64-bit contents returned as [int].
  int _doubleBitsToInt(double value) {
    var scratchpad = ByteData(8)..setFloat64(0, value);
    return scratchpad.getInt64(0);
  }

  /// The byte offset of this [Block] in the VMO (calculated from its [index]).
  int get _offset => index * bytesPerIndex;

  /// The byte offset of this [Block]'s payload in the VMO.
  int get _payloadOffset => _offset + headerSizeBytes;
}
