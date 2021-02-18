// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' show min;

import 'block.dart';
import 'vmo_fields.dart';
import 'vmo_holder.dart';
import 'vmo_writer.dart';

// All sizes are in bytes.
const int _pageSizeBytes = 4096;

/// With this allocator, all allocated blocks in the VMO will be 32 bytes.
/// (Indexes 0, 1, and 2 are reserved and not allocated.)
const int defaultBlockOrder = 1;

/// Represents a heap allocator that allocates blocks out of a VMO.  Allocation
/// happens one fixed-size block at a time, so repeated calls may be needed to
/// allocate a large amount of memory.
abstract class Heap {
  /// Allocates a block using the bytes size hint. If [required] is not set,
  /// the allocated block size may be smaller than the size hint, but is the
  /// largest size block that the allocator can provide.  If [required] is set,
  /// allocation will fail if unable to provide a block at least the size of
  /// the hint.
  Block? allocateBlock(int bytesHint, {bool required = false});

  /// Frees the previously allocated block.
  void freeBlock(Block block);
}

/// The base class for which log writers will inherit from.
///
/// (The current implementation is a barely-MVP heap: a 32-byte-block slab
/// allocator.)
class Slab32 implements Heap {
  /// Size in bytes of the touched / visited subset of the VMO incorporated in
  /// the data structure.
  int? _currentSizeBytes;

  final VmoHolder _vmo;

  /// Index of the first block on the freelist.
  int _freelistHead = invalidIndex;

  /// Construct VMO with max available size; initialize startingSize of it.
  Slab32(this._vmo) {
    _currentSizeBytes = min(_pageSizeBytes, _vmo.size);
    _addFreelistBlocks(
        fromBytes: heapStartIndex * bytesPerIndex, toBytes: _currentSizeBytes!);
  }

  /// Creates a new Heap over the supplied VMO holder.
  static Heap create(VmoHolder vmo) => Slab32(vmo);

  /// Gets a block from the freelist, or null if none available.
  @override
  Block? allocateBlock(int unused, {bool required = false}) {
    if (_freelistHead == invalidIndex) {
      // Grow one page at a time to save RAM.
      _growHeap(_currentSizeBytes! + _pageSizeBytes);
    }
    if (_freelistHead == invalidIndex) {
      return null;
    }
    var block = Block.read(_vmo, _freelistHead);
    _freelistHead = block.nextFree;
    block.becomeReserved();
    return block;
  }

  /// Returns a [block] to the freelist.
  @override
  void freeBlock(Block block) {
    if (block.type == BlockType.header || block.type == BlockType.free) {
      throw ArgumentError("I shouldn't be trying to free this type "
          '(index ${block.index}, type ${block.type})');
    }
    if (block.index < heapStartIndex ||
        block.index * bytesPerIndex >= _currentSizeBytes!) {
      throw ArgumentError('Tried to free bad index ${block.index}');
    }
    block.becomeFree(_freelistHead); // Do we want to zero its contents?
    _freelistHead = block.index;
  }

  void _addFreelistBlocks({required int fromBytes, required int toBytes}) {
    for (int i = fromBytes ~/ bytesPerIndex;
        i < toBytes ~/ bytesPerIndex;
        i += 2) {
      Block.create(_vmo, i, order: defaultBlockOrder).becomeFree(_freelistHead);
      _freelistHead = i;
    }
  }

  /// Expands the heap to occupy more of the VMO.
  ///
  /// Touches memory, causing actual RAM to be used; that's why the heap
  /// starts small and grows as needed.
  void _growHeap(int desiredSizeBytes) {
    if (_currentSizeBytes == _vmo.size) {
      return; // Fail silently.
    }
    int newSize = desiredSizeBytes;
    if (newSize > _vmo.size) {
      newSize = _vmo.size;
    }
    _addFreelistBlocks(fromBytes: _currentSizeBytes!, toBytes: newSize);
    _currentSizeBytes = newSize;
  }
}
