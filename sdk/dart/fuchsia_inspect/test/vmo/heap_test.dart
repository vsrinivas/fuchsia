// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:fuchsia_inspect/src/vmo/block.dart';
import 'package:fuchsia_inspect/src/vmo/heap.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_fields.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

import '../util.dart';

// In the VMO data structure, indexes 0..3 are reserved for VMO HEADER block,
// root-node block, and root-node's name.
//
// A 96-byte heap holds indexes 0..5 (at 16 bytes per index).
// Since allocated blocks are 32 bytes, the first allocated block will be at
// index 2, and the second will be at index 4.
//
// Two allocated blocks should be enough to test the data structure
// algorithms, so the heap will be conveniently small at 96 bytes, and
// we'll expect to see 2 and 4 as valid allocated indexes.
const int _heapSizeBytes = 96;
const List<int> _allocatedIndexes = [2, 4];

void main() {
  group('In the Heap', () {
    test('the initial free state is correct in the VMO', () {
      var vmo = FakeVmoHolder(_heapSizeBytes);
      Slab32(vmo);
      var f = hexChar(BlockType.free.value);
      compare(vmo, 0x00, '0  0 000000 00000000  00000000 00000000');
      compare(vmo, 0x10, '0  0 000000 00000000  00000000 00000000');
      compare(vmo, 0x20, '01 0$f 0000 00000000  00000000 00000000');
      compare(vmo, 0x30, '0  0 000000 00000000  00000000 00000000');
      compare(vmo, 0x40, '01 0$f 0_00 00000000  00000000 00000000');
      compare(vmo, 0x50, '0  0 000000 00000000  00000000 00000000');
    });

    test('allocate changes VMO contents correctly', () {
      var vmo = FakeVmoHolder(_heapSizeBytes);
      var heap = Slab32(vmo);
      var blocks = _allocateEverything(heap);
      expect(blocks, hasLength(2));
      var r = hexChar(BlockType.reserved.value);
      compare(vmo, 0x20, '01 0$r 0_00 00000000  00000000 00000000');
      compare(vmo, 0x30, '0  0 000000 00000000  00000000 00000000');
      compare(vmo, 0x40, '01 0$r 0_00 00000000  00000000 00000000');
      compare(vmo, 0x50, '0  0 000000 00000000  00000000 00000000');
    });

    test('free and re-allocate work correctly', () {
      var vmo = FakeVmoHolder(_heapSizeBytes);
      var heap = Slab32(vmo);
      var blocks = _allocateEverything(heap);
      expect(blocks, hasLength(_allocatedIndexes.length));
      // Free one, get it back
      heap.freeBlock(blocks.removeLast());
      var lastBlock = _allocateEverything(heap);
      expect(lastBlock, hasLength(1));
      // Free in reverse order to mix up the list
      heap..freeBlock(blocks.removeLast())..freeBlock(lastBlock.removeLast());
      blocks = _allocateEverything(heap);
      // Should get two blocks again
      expect(blocks, hasLength(_allocatedIndexes.length));
    });
  });
}

List<Block> _allocateEverything(Heap heap) {
  var blocks = <Block>[];
  for (Block? block = heap.allocateBlock(32);
      block != null;
      block = heap.allocateBlock(32)) {
    blocks.add(block);
  }
  // Make sure we're actually getting unique blocks
  expect(Set.of(blocks.map((block) => block.index)), hasLength(blocks.length));
  // With a heapSize-byte VMO, valid indexes are only 2 and 4 (see comment above).
  for (var block in blocks) {
    expect(block.index, isIn(_allocatedIndexes));
  }
  return blocks;
}
