// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:typed_data';

import 'package:fuchsia_inspect/src/vmo/block.dart';
import 'package:fuchsia_inspect/src/vmo/util.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_fields.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

import '../util.dart';

void main() {
  group('Block', () {
    test('accepts state (type) correctly', () {
      _accepts('lock/unlock', [BlockType.header], (block) {
        block.lock();
        block.unlock();
      });
      _accepts(
          'becomeNode', [BlockType.anyValue], (block) => block.becomeNode());
      _accepts('becomeBuffer', [BlockType.anyValue],
          (block) => block.becomeBuffer());
      _accepts('setChildren', [BlockType.nodeValue, BlockType.tombstone],
          (block) => block.childCount = 0);
      _accepts('getChildren', [BlockType.nodeValue, BlockType.tombstone],
          (block) => block.childCount);

      _accepts('setBufferTotalLength', [BlockType.bufferValue],
          (block) => block.bufferTotalLength = 0);
      _accepts('getBufferTotalLength', [BlockType.bufferValue],
          (block) => block.bufferTotalLength);
      _accepts('setBufferExtentIndex', [BlockType.bufferValue],
          (block) => block.bufferExtentIndex = 0);
      _accepts('getBufferExtentIndex', [BlockType.bufferValue],
          (block) => block.bufferExtentIndex);
      _accepts('setBufferFlags', [BlockType.bufferValue],
          (block) => block.bufferFlags = 0);
      _accepts('getBufferFlags', [BlockType.bufferValue],
          (block) => block.bufferFlags);

      _accepts('becomeTombstone', [BlockType.nodeValue],
          (block) => block.becomeTombstone());
      _accepts('becomeReserved', [BlockType.free],
          (block) => block.becomeReserved());
      _accepts('nextFree', [BlockType.free], (block) => block.nextFree);
      _accepts('becomeValue', [BlockType.reserved],
          (block) => block.becomeValue(nameIndex: 1, parentIndex: 2));
      _accepts(
          'nameIndex',
          [
            BlockType.nodeValue,
            BlockType.anyValue,
            BlockType.bufferValue,
            BlockType.intValue,
            BlockType.doubleValue,
            BlockType.boolValue
          ],
          (block) => block.nameIndex);
      _accepts(
          'parentIndex',
          [
            BlockType.nodeValue,
            BlockType.anyValue,
            BlockType.bufferValue,
            BlockType.intValue,
            BlockType.doubleValue,
            BlockType.boolValue
          ],
          (block) => block.parentIndex);
      _accepts('becomeDoubleMetric', [BlockType.anyValue],
          (block) => block.becomeDoubleMetric(0.0));
      _accepts('becomeIntMetric', [BlockType.anyValue],
          (block) => block.becomeIntMetric(0));
      _accepts('becomeBoolMetric', [BlockType.anyValue],
          (block) => block.becomeBoolMetric(false));
      _accepts('intValueGet', [BlockType.intValue], (block) => block.intValue);
      _accepts(
          'intValueSet', [BlockType.intValue], (block) => block.intValue = 0);
      _accepts('doubleValueGet', [BlockType.doubleValue],
          (block) => block.doubleValue);
      _accepts('doubleValueSet', [BlockType.doubleValue],
          (block) => block.doubleValue = 0.0);
      _accepts('becomeName', [BlockType.reserved],
          (block) => block.becomeName('foo'));
      _accepts('extentIndexSet', [BlockType.bufferValue],
          (block) => block.bufferExtentIndex = 0);
      _accepts(
          'nextExtentGet', [BlockType.extent], (block) => block.nextExtent);
    });

    test('can read, including payload bits', () {
      final vmo = FakeVmoHolder(32);
      vmo.bytes
        ..setUint8(0, 0x01)
        ..setUint8(1, BlockType.bufferValue.value)
        ..setUint8(2, 0x14) // Parent index should be 0x14
        ..setUint8(5, 0x32) // Name index should be 0x32
        ..setUint8(8, 0x7f) // Length should be 0x7f
        ..setUint8(12, 0x0a) // Extent
        ..setUint8(15, 0xb0); // Flags 0xb
      final p = hexChar(BlockType.bufferValue.value);
      compare(vmo, 0, '01 0$p 14 00  00 32 00 00  7f00 0000 0a00 00b0');
      final block = Block.read(vmo, 0);
      expect(block.size, 32);
      expect(block.type.value, BlockType.bufferValue.value);
      expect(block.parentIndex, 0x14);
      expect(block.nameIndex, 0x32);
      expect(block.bufferTotalLength, 0x7f);
      expect(block.bufferExtentIndex, 0xa);
      expect(block.bufferFlags, 0xb);
    });

    test('can read, including payload bytes', () {
      final vmo = FakeVmoHolder(32);
      vmo.bytes
        ..setUint8(0, 0x01)
        ..setUint8(1, BlockType.nameUtf8.value)
        ..setUint8(2, 0x02) // Set length to 2
        ..setUint8(8, 0x41) // 'a'
        ..setUint8(9, 0x42); // 'b'
      final n = hexChar(BlockType.nameUtf8.value);
      compare(vmo, 0, '01 0$n 02 00 0000 0000  4142 0000 0000 0000 0000');
      final block = Block.read(vmo, 0);
      expect(block.size, 32);
      expect(block.type.value, BlockType.nameUtf8.value);
      expect(block.payloadBytes.getUint8(0), 0x41);
      expect(block.payloadBytes.getUint8(1), 0x42);
    });
  });

  group('Block operations write to VMO correctly:', () {
    test('Creating, locking, and unlocking the VMO header', () {
      final vmo = FakeVmoHolder(32);
      final block = Block.create(vmo, 0)..becomeHeader();
      final h = hexChar(BlockType.header.value);
      compare(vmo, 0, '00 0$h 0100 49 4E 53 50  0000 0000 0000 0000');
      block.lock();
      compare(vmo, 0, '00 0$h 0100 49 4E 53 50  0100 0000 0000 0000');
      block.unlock();
      compare(vmo, 0, '00 0$h 0100 49 4E 53 50  0200 0000 0000 0000');
    });

    test('Becoming and modifying an intValue via free, reserved, anyValue', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)..becomeFree(5);
      final f = hexChar(BlockType.free.value);
      compare(vmo, 32, '01 0$f 05 00 0000 0000');
      expect(block.nextFree, 5);
      block.becomeReserved();
      compare(vmo, 32, '01 0${hexChar(BlockType.reserved.value)}');
      block.becomeValue(parentIndex: 0xbc, nameIndex: 0x7d);
      final a = hexChar(BlockType.anyValue.value);
      final i = hexChar(BlockType.intValue.value);
      compare(vmo, 32, '01 0$a bc 00 00 7d 00 00');
      block.becomeIntMetric(0xbeef);
      compare(vmo, 32, '01 0$i bc 00 00 7d 00 00 efbe');
      block.intValue += 1;
      compare(vmo, 32, '01 0$i bc 00 00 7d 00 00 f0be');
    });

    test('Becoming a nodeValue and then a tombstone', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)
        ..becomeFree(5)
        ..becomeReserved()
        ..becomeValue(parentIndex: 0xbc, nameIndex: 0x7d)
        ..becomeNode();
      final n = hexChar(BlockType.nodeValue.value);
      compare(vmo, 32, '01 0$n bc 00 00 7d 00 00 0000');
      block.childCount += 1;
      compare(vmo, 32, '01 0$n bc 00 00 7d 00 00 0100');
      block.becomeTombstone();
      final t = hexChar(BlockType.tombstone.value);
      compare(vmo, 32, '01 0$t bc 00 00 7d 00 00 0100');
    });

    test('Becoming and modifying doubleValue', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)
        ..becomeFree(5)
        ..becomeReserved()
        ..becomeValue(parentIndex: 0xbc, nameIndex: 0x7d)
        ..becomeDoubleMetric(1.0);
      final d = hexChar(BlockType.doubleValue.value);
      compare(vmo, 32, '01 0$d bc 00 00 7d 00 00');
      expect(vmo.bytes.getFloat64(40, Endian.little), 1.0);
      block.doubleValue++;
      expect(vmo.bytes.getFloat64(40, Endian.little), 2.0);
    });

    test('Becoming and modifying a bufferValue', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)
        ..becomeFree(5)
        ..becomeReserved()
        ..becomeValue(parentIndex: 0xbc, nameIndex: 0x7d)
        ..becomeBuffer();
      final p = hexChar(BlockType.bufferValue.value);
      compare(vmo, 32, '01 0$p bc 00 00 7d 00 00 00 00 00 00  00 00 00 00');
      block
        ..bufferExtentIndex = 0x35
        ..bufferTotalLength = 0x17b
        ..bufferFlags = 0xa;
      compare(vmo, 32, '01 0$p bc 00 00 7d 00 00 7b 01 00 00  35 00 00 a0');
      expect(block.bufferTotalLength, 0x17b);
      expect(block.bufferExtentIndex, 0x35);
      expect(block.bufferFlags, 0xa);
    });

    test('Becoming a name', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)..becomeName('abc');
      final n = hexChar(BlockType.nameUtf8.value);
      compare(vmo, 32, '01 0$n 03 00 0000 0000 61 62 63');
      expect(
          Uint8List.view(
              block.nameUtf8.buffer, 0, block.nameUtf8.lengthInBytes),
          Uint8List.fromList([0x61, 0x62, 0x63]));
    });

    test('Becoming and setting an extent', () {
      final vmo = FakeVmoHolder(64);
      final block = Block.create(vmo, 2)
        ..becomeFree(4)
        ..becomeReserved()
        ..becomeExtent(0x42)
        ..setExtentPayload(toByteData('abc'));
      final e = hexChar(BlockType.extent.value);
      compare(vmo, 32, '01 0$e 42 00 0000 0000 61 62 63');
      expect(block.nextExtent, 0x42);
      expect(block.payloadSpaceBytes, block.size - headerSizeBytes);
    });
  });
}

/// Verify which block types are accepted by which functions.
///
/// For all block types (including anyValue), creates a block of that type and
///  passes it to [testFunction].
/// [previousStates] contains the types that should not throw
/// an error. All others should throw.
void _accepts(String testName, List<BlockType> previousStates, testFunction) {
  final vmo = FakeVmoHolder(4096);
  for (BlockType type in BlockType.values) {
    final block = Block.createWithType(vmo, 0, type);
    if (previousStates.contains(type)) {
      expect(() => testFunction(block), returnsNormally,
          reason: '$testName should have accepted type $type');
    } else {
      expect(() => testFunction(block), throwsA(anything),
          reason: '$testName should not accept type $type');
    }
  }
}
