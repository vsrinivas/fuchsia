// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
// ignore_for_file: avoid_catching_errors

import 'dart:io';
import 'dart:typed_data';

import 'package:fuchsia_inspect/src/vmo/bitfield64.dart';
import 'package:fuchsia_inspect/src/vmo/block.dart';
import 'package:fuchsia_inspect/src/vmo/heap.dart';
import 'package:fuchsia_inspect/src/vmo/little_big_slab.dart';
import 'package:fuchsia_inspect/src/vmo/util.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_fields.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_writer.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

import '../util.dart';

void main() {
  group('LittleBigSlab //', () {
    VmoWriter createWriter(VmoHolder vmo) {
      return VmoWriter(vmo, LittleBigSlab.create);
    }

    group('vmo_writer operations work:', () {
      test('Init VMO writes correctly to the VMO', () {
        final vmo = FakeVmoHolder(256);
        createWriter(vmo);
        final f = hexChar(BlockType.free.value);
        final h = hexChar(BlockType.header.value);
        compare(vmo, 0x00, '00 0$h 0100 494E5350  00000000 00000000');
        compare(vmo, 0x10, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x20, '01 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x30, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x40, '02 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x50, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x60, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x70, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x80, '02 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x90, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0xa0, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0xb0, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0xc0, '02 0$f 0_00 00000000  00000000 00000000');
        // Number of free blocks has been verified manually here.
        expect(countFreeBlocks(vmo), 5, reason: dumpBlocks(vmo));
      });

      test('failed createNode leaves VMO sequence number even (valid VMO)', () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = createWriter(vmo);
        final h = hexChar(BlockType.header.value);
        writer.createNode(writer.rootNode, 'child');
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
        writer.createBufferProperty(writer.rootNode, 'property');
      });

      test(
          'failed createBufferProperty leaves VMO sequence number even (valid VMO)',
          () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = VmoWriter(vmo, Slab32.create);
        final h = hexChar(BlockType.header.value);
        writer.createBufferProperty(writer.rootNode, 'property');
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
      });

      test('failed createMetric leaves VMO sequence number even (valid VMO)',
          () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = createWriter(vmo);
        final h = hexChar(BlockType.header.value);
        writer.createMetric(writer.rootNode, 'metric', 0);
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
      });

      test('make, modify, and free Node', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final child = writer.createNode(writer.rootNode, 'child');
        checker.check(2, [Test(_nameFor(vmo, child), toByteData('child'))]);
        writer.deleteEntity(child);
        // Deleting a node without children should free it and its name.
        checker.check(-2, []);
      });

      test('make, modify, and free IntMetric', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final intMetric = writer.createMetric(writer.rootNode, 'intMetric', 1);
        checker.check(2, [
          Test(_nameFor(vmo, intMetric), toByteData('intMetric')),
          Test(intMetric, 1, reason: 'int value wrong')
        ]);
        writer.addMetric(intMetric, 2);
        checker.check(0, [Test(intMetric, 3)]);
        writer.subMetric(intMetric, 4);
        checker.check(0, [Test(intMetric, -1)]);
        writer.setMetric(intMetric, 2);
        checker.check(0, [Test(intMetric, 2)]);
        writer.deleteEntity(intMetric);
        checker.check(-2, []);
      });

      test('make, modify, and free DoubleMetric', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final doubleMetric =
            writer.createMetric(writer.rootNode, 'doubleMetric', 1.5);
        checker.check(2, [
          Test(_nameFor(vmo, doubleMetric), toByteData('doubleMetric')),
          Test(doubleMetric, 1.5, reason: 'double value wrong')
        ]);
        writer.addMetric(doubleMetric, 2.0);
        checker.check(0, [Test(doubleMetric, 3.5)]);
        writer.subMetric(doubleMetric, 4.0);
        checker.check(0, [Test(doubleMetric, -0.5)]);
        writer.setMetric(doubleMetric, 1.5);
        checker.check(0, [Test(doubleMetric, 1.5)]);
        writer.deleteEntity(doubleMetric);
        checker.check(-2, []);
      });

      test('make, modify, and free Property', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);

        final property = writer.createBufferProperty(writer.rootNode, 'prop');
        checker.check(2, [Test(_nameFor(vmo, property), toByteData('prop'))]);
        final bytes = ByteData(8)..setFloat64(0, 1.23);
        writer.setBufferProperty(property, bytes);
        // Same number of free blocks, since a large one is split.
        checker.check(0, [Test(_extentFor(vmo, property), bytes)]);
        writer.deleteEntity(property);
        // Property, its extent, and its name should be freed.
        checker.check(-3, []);
      });

      test('Node delete permutations', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final parent = writer.createNode(writer.rootNode, 'parent');
        checker.check(2, []);
        final child = writer.createNode(parent, 'child');
        checker.check(1, [Test(parent, 1)]);
        final metric = writer.createMetric(child, 'metric', 1);
        checker.check(2, [Test(child, 1), Test(parent, 1)]);
        writer.deleteEntity(child);
        // Now child should be a tombstone; only its name should be freed.
        checker.check(-1, [Test(child, 1), Test(parent, 0)]);
        writer.deleteEntity(parent);
        checker.check(-2, []);
        writer.deleteEntity(metric);
        // Metric, its name, and child should be freed.
        checker.check(-3, []);
        // Make sure we can still create nodes on the root
        final newMetric =
            writer.createMetric(writer.rootNode, 'newIntMetric', 42);
        checker.check(2, [
          Test(_nameFor(vmo, newMetric), toByteData('newIntMetric')),
          Test(newMetric, 42),
        ]);
      });

      test('String property has string flag bits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        writer.setBufferProperty(property, 'abc');
        expect(Block.read(vmo, property).bufferFlags, propertyUtf8Flag);
      });

      test('Binary property has binary flag bits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        writer.setBufferProperty(property, ByteData(3));
        expect(Block.read(vmo, property).bufferFlags, propertyBinaryFlag);
      });

      test('Invalid property-set value type throws ArgumentError', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        expect(() => writer.setBufferProperty(property, 3),
            throwsA(const TypeMatcher<ArgumentError>()));
      });

      test('Large properties', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        int unique = 2;
        void fill(ByteData data) {
          for (int i = 0; i < data.lengthInBytes; i += 2) {
            data.setUint16(i, unique);
            unique += 1;
          }
        }

        try {
          final data0 = ByteData(0);
          final data200 = ByteData(200);
          fill(data200);
          final data230 = ByteData(230);
          fill(data230);
          final data530 = ByteData(530);
          fill(data530);
          final property =
              writer.createBufferProperty(writer.rootNode, 'property');
          expect(VmoMatcher(vmo).node()..propertyEquals('property', ''),
              hasNoErrors);

          writer.setBufferProperty(property, data200);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data200.buffer.asUint8List()),
              hasNoErrors);

          // There isn't space for 200+230, but the set to 230 should still work.
          writer.setBufferProperty(property, data230);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data230.buffer.asUint8List()),
              hasNoErrors);

          // The set to 530 should fail and leave an empty property.
          writer.setBufferProperty(property, data530);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data0.buffer.asUint8List()),
              hasNoErrors);

          // And after all that, 200 should still work.
          writer.setBufferProperty(property, data200);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data200.buffer.asUint8List()),
              hasNoErrors);
        } on StateError {
          stdout.write('Test failure caused a VMO dump.\n${dumpBlocks(vmo)}');
          rethrow;
        }
      });
      test('Node and property name length limits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);

        // Note that the node and property names are truncated.
        final node = writer.createNode(writer.rootNode,
            'N012345678901234567890123456789012345678901234567890123456789000');
        checker.check(2, [
          Test(
              _nameFor(vmo, node),
              toByteData(
                  'N0123456789012345678901234567890123456789012345678901'))
        ]);

        final property = writer.createBufferProperty(node,
            'PP012345678901234567890123456789012345678901234567890123456789000');
        checker.check(1, [
          Test(
              _nameFor(vmo, property),
              toByteData(
                  'PP012345678901234567890123456789012345678901234567890'))
        ]);
      });
    });
  });
  group('Slab32 //', () {
    VmoWriter createWriter(VmoHolder vmo) {
      return VmoWriter(vmo, Slab32.create);
    }

    group('vmo_writer operations work:', () {
      test('Init VMO writes correctly to the VMO', () {
        final vmo = FakeVmoHolder(256);
        createWriter(vmo);
        final f = hexChar(BlockType.free.value);
        final h = hexChar(BlockType.header.value);
        compare(vmo, 0x00, '00 0$h 0100 494E5350  00000000 00000000');
        compare(vmo, 0x10, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x20, '01 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x30, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x40, '01 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x50, '0  0 000000 00000000  00000000 00000000');
        compare(vmo, 0x60, '01 0$f 0_00 00000000  00000000 00000000');
        compare(vmo, 0x70, '0  0 000000 00000000  00000000 00000000');
        expect(countFreeBlocks(vmo), 8, reason: dumpBlocks(vmo));
      });

      test('failed createNode leaves VMO sequence number even (valid VMO)', () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = createWriter(vmo);
        final h = hexChar(BlockType.header.value);
        writer.createNode(writer.rootNode, 'child');
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
        writer.createBufferProperty(writer.rootNode, 'property');
      });

      test(
          'failed createBufferProperty leaves VMO sequence number even (valid VMO)',
          () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = VmoWriter(vmo, Slab32.create);
        final h = hexChar(BlockType.header.value);
        writer.createBufferProperty(writer.rootNode, 'property');
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
      });

      test('failed createMetric leaves VMO sequence number even (valid VMO)',
          () {
        final vmo = FakeVmoHolder(64); // No space for anything
        final writer = createWriter(vmo);
        final h = hexChar(BlockType.header.value);
        writer.createMetric(writer.rootNode, 'metric', 0);
        compare(vmo, 0x00, '00 0$h 0100 494E5350  02000000 00000000');
      });

      test('make, modify, and free Node', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final child = writer.createNode(writer.rootNode, 'child');
        checker.check(2, [Test(_nameFor(vmo, child), toByteData('child'))]);
        writer.deleteEntity(child);
        // Deleting a node without children should free it and its name.
        // root node should have 0 children.
        checker.check(-2, []);
      });

      test('make, modify, and free IntMetric', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final intMetric = writer.createMetric(writer.rootNode, 'intMetric', 1);
        checker.check(2, [
          Test(_nameFor(vmo, intMetric), toByteData('intMetric')),
          Test(intMetric, 1, reason: 'int value wrong')
        ]);
        writer.addMetric(intMetric, 2);
        checker.check(0, [Test(intMetric, 3)]);
        writer.subMetric(intMetric, 4);
        checker.check(0, [Test(intMetric, -1)]);
        writer.setMetric(intMetric, 2);
        checker.check(0, [Test(intMetric, 2)]);
        writer.deleteEntity(intMetric);
        checker.check(-2, []);
      });

      test('make, modify, and free DoubleMetric', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final doubleMetric =
            writer.createMetric(writer.rootNode, 'doubleMetric', 1.5);
        checker.check(2, [
          Test(_nameFor(vmo, doubleMetric), toByteData('doubleMetric')),
          Test(doubleMetric, 1.5, reason: 'double value wrong')
        ]);
        writer.addMetric(doubleMetric, 2.0);
        checker.check(0, [Test(doubleMetric, 3.5)]);
        writer.subMetric(doubleMetric, 4.0);
        checker.check(0, [Test(doubleMetric, -0.5)]);
        writer.setMetric(doubleMetric, 1.5);
        checker.check(0, [Test(doubleMetric, 1.5)]);
        writer.deleteEntity(doubleMetric);
        checker.check(-2, []);
      });

      test('make, modify, and free Property', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);

        final property = writer.createBufferProperty(writer.rootNode, 'prop');
        checker.check(2, [Test(_nameFor(vmo, property), toByteData('prop'))]);
        final bytes = ByteData(8)..setFloat64(0, 1.23);
        writer.setBufferProperty(property, bytes);
        checker.check(1, [Test(_extentFor(vmo, property), bytes)]);
        writer.deleteEntity(property);
        // Property, its extent, and its name should be freed. Its parent should
        // have one fewer children (0 in this case).
        checker.check(-3, []);
      });

      test('Node delete permutations', () {
        final vmo = FakeVmoHolder(1024);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);
        final parent = writer.createNode(writer.rootNode, 'parent');
        checker.check(2, []);
        final child = writer.createNode(parent, 'child');
        checker.check(2, [Test(parent, 1)]);
        final metric = writer.createMetric(child, 'metric', 1);
        checker.check(2, [Test(child, 1), Test(parent, 1)]);
        writer.deleteEntity(child);
        // Now child should be a tombstone; only its name should be freed.
        checker.check(-1, [Test(child, 1), Test(parent, 0)]);
        writer.deleteEntity(parent);
        // Parent, plus its name, should be freed; root should have no children.
        checker.check(-2, []);
        writer.deleteEntity(metric);
        // Metric, its name, and child should be freed.
        checker.check(-3, []);
        // Make sure we can still create nodes on the root
        final newMetric =
            writer.createMetric(writer.rootNode, 'newIntMetric', 42);
        checker.check(2, [
          Test(_nameFor(vmo, newMetric), toByteData('newIntMetric')),
          Test(newMetric, 42)
        ]);
      });

      test('String property has string flag bits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        writer.setBufferProperty(property, 'abc');
        expect(Block.read(vmo, property).bufferFlags, propertyUtf8Flag);
      });

      test('Binary property has binary flag bits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        writer.setBufferProperty(property, ByteData(3));
        expect(Block.read(vmo, property).bufferFlags, propertyBinaryFlag);
      });

      test('Invalid property-set value type throws ArgumentError', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final property =
            writer.createBufferProperty(writer.rootNode, 'property');
        expect(() => writer.setBufferProperty(property, 3),
            throwsA(const TypeMatcher<ArgumentError>()));
      });

      test('Large properties', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        int unique = 2;
        void fill(ByteData data) {
          for (int i = 0; i < data.lengthInBytes; i += 2) {
            data.setUint16(i, unique);
            unique += 1;
          }
        }

        try {
          final data0 = ByteData(0);
          final data200 = ByteData(200);
          fill(data200);
          final data230 = ByteData(230);
          fill(data230);
          final data530 = ByteData(530);
          fill(data530);
          final property =
              writer.createBufferProperty(writer.rootNode, 'property');
          expect(VmoMatcher(vmo).node().propertyEquals('property', ''),
              hasNoErrors);

          writer.setBufferProperty(property, data200);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data200.buffer.asUint8List()),
              hasNoErrors);

          // There isn't space for 200+230, but the set to 230 should still work.
          writer.setBufferProperty(property, data230);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data230.buffer.asUint8List()),
              hasNoErrors);

          // The set to 530 should fail and leave an empty property.
          writer.setBufferProperty(property, data530);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data0.buffer.asUint8List()),
              hasNoErrors);

          // And after all that, 200 should still work.
          writer.setBufferProperty(property, data200);
          expect(
              VmoMatcher(vmo)
                  .node()
                  .propertyEquals('property', data200.buffer.asUint8List()),
              hasNoErrors);
        } on StateError {
          stdout.write('Test failure caused a VMO dump.\n${dumpBlocks(vmo)}');
          rethrow;
        }
      });
      test('Node and property name length limits', () {
        final vmo = FakeVmoHolder(512);
        final writer = createWriter(vmo);
        final checker = Checker(vmo)..check(0, []);

        // Note that the names are truncated.
        final node = writer.createNode(writer.rootNode,
            'N012345678901234567890123456789012345678901234567890123456789000');
        checker.check(2, [
          Test(_nameFor(vmo, node), toByteData('N01234567890123456789012'))
        ]);

        final property = writer.createBufferProperty(node,
            'PP012345678901234567890123456789012345678901234567890123456789000');
        checker.check(2, [
          Test(_nameFor(vmo, property), toByteData('PP0123456789012345678901'))
        ]);
      });
    });
  });
}

/// Counts the free blocks in this VMO.
///
/// NOTE: This is O(n) in the size of the VMO. Be careful not to do n^2
/// algorithms on large VMOs.
int countFreeBlocks(VmoHolder vmo) {
  var blocksFree = 0;
  for (int i = 0; i < vmo.size;) {
    var b = Bitfield64(vmo.readInt64(i));
    if (b.read(typeBits) == BlockType.free.value) {
      blocksFree++;
    }
    i += 1 << 4 + b.read(orderBits);
  }
  return blocksFree;
}

/// Gets the index of the [BlockType.name] block of the Value at [index].
int _nameFor(vmo, index) => Block.read(vmo, index).nameIndex;

/// Gets the index of the first [BlockType.extent] block of the Value at
/// [index].
int _extentFor(vmo, index) => Block.read(vmo, index).bufferExtentIndex;

/// Test holds values for use in [Checker.check()].
class Test {
  final int index;
  final String? reason;
  final dynamic value;
  Test(this.index, this.value, {this.reason});
}

/// Checker tracks activity on a VMO and makes sure its state is correct.
///
/// check() must be called once after VmoWriter initialization, and once after
/// every operation that changes the VMO lock.
class Checker {
  final FakeVmoHolder _vmo;
  int nextLock = 0;
  late int expectedFree;
  Checker(this._vmo) {
    expectedFree = countFreeBlocks(_vmo);
  }

  void testPayload(Test test) {
    int payloadOffset = test.index * 16 + 8;
    var commonString = '${payloadOffset.toRadixString(16)} '
        '(index ${(payloadOffset - 8) >> 4})';
    if (test.reason != null) {
      commonString += ' because ${test.reason}';
    }
    final value = test.value;
    if (value is int) {
      int intValue = value;
      expect(_vmo.bytes.getUint64(payloadOffset, Endian.little), intValue,
          reason: 'int at $commonString\n${dumpBlocks(_vmo)}');
    } else if (value is double) {
      double doubleValue = value;
      expect(_vmo.bytes.getFloat64(payloadOffset, Endian.little), doubleValue,
          reason: 'double at $commonString\n${dumpBlocks(_vmo)}');
    } else if (value is ByteData) {
      ByteData byteData = value;
      final actual = _vmo.bytes.buffer
          .asUint8List(payloadOffset, byteData.buffer.lengthInBytes);
      expect(actual, byteData.buffer.asUint8List(),
          reason: 'ByteData at $commonString\n${dumpBlocks(_vmo)}');
    } else {
      throw ArgumentError("I can't handle value type ${value.runtimeType}");
    }
  }

  void check(int usedBlocks, List<Test> checks) {
    expect(_vmo.bytes.getInt64(8, Endian.little), nextLock,
        reason: 'Lock out of sync (call check() once per operation)');
    nextLock += 2;
    expectedFree -= usedBlocks;
    expect(countFreeBlocks(_vmo), expectedFree,
        reason: 'Number of free blocks mismatched.\n${dumpBlocks(_vmo)}');
    checks.forEach(testPayload);
  }
}
