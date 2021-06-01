// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_inspect/src/inspect/internal/_inspect_impl.dart';
import 'package:fuchsia_inspect/src/vmo/util.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_holder.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_writer.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

void main() {
  late VmoHolder vmo;
  late InspectImpl inspect;
  late Node root;

  setUp(() {
    vmo = FakeVmoHolder(512);
    var writer = VmoWriter.withVmo(vmo);
    inspect = InspectImpl(writer)
      ..healthWithNanosForTest(() {
        return 42;
      });
    root = inspect.root!;
  });

  test('Child nodes have unique indices from their parents', () {
    var childNode = root.child('banana')!;

    expect(childNode, isNotNull);
    expect(childNode.index, isNot(root.index));
  });

  test('Child nodes created twice return the same object', () {
    var childNode = root.child('banana');
    var childNode2 = root.child('banana');

    expect(childNode, isNotNull);
    expect(childNode2, isNotNull);
    expect(childNode, same(childNode2));
  });

  test('Nodes created after deletion return different objects', () {
    var childNode = root.child('banana')!..delete();
    var childNode2 = root.child('banana');

    expect(childNode, isNotNull);
    expect(childNode2, isNotNull);
    expect(childNode, isNot(childNode2));
  });

  test('Child nodes have unique indices from their siblings', () {
    var child1 = root.child('thing1')!;
    var child2 = root.child('thing2')!;

    expect(child1.index, isNot(child2.index));
  });

  test('Deleting root node has no effect', () {
    root.delete();
    var _ = root.child('sheep');
    expect(VmoMatcher(vmo).node().at(['sheep']), hasNoErrors);
  });

  test('deleted node is a no-op', () {
    final node = Node.deleted();
    expect(node.valid, false);
  });

  group('Deleted node tests:', () {
    late Node deletedNode;

    setUp(() {
      deletedNode = root.child('sheep')!..delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(deletedNode.valid, false);
    });

    test('can be deleted (more than once)', () {
      var child = deletedNode.child('sheep')!..delete();
      expect(child.valid, false);
      expect(() => deletedNode.delete(), returnsNormally);
      expect(() => child.delete(), returnsNormally);
    });

    test('Creating a child on an already deleted node is a no-op', () {
      late Node grandchild;
      expect(() => grandchild = deletedNode.child('404')!, returnsNormally);
      expect(() => grandchild.child('404')!, returnsNormally);
      expect(grandchild.valid, false);
    });

    test('Creating an IntProperty on an already deleted node is a no-op', () {
      late IntProperty property;
      expect(() => property = deletedNode.intProperty('404')!, returnsNormally);
      expect(() => property.setValue(404), returnsNormally);
      expect(property.valid, false);
    });

    test('Creating a DoubleProperty on an already deleted node is a no-op', () {
      late DoubleProperty property;
      expect(
          () => property = deletedNode.doubleProperty('404')!, returnsNormally);
      expect(() => property.setValue(404), returnsNormally);
      expect(property.valid, false);
    });

    test('Creating a StringProperty on an already deleted node is a no-op', () {
      late StringProperty property;
      expect(
          () => property = deletedNode.stringProperty('404')!, returnsNormally);
      expect(() => property.setValue('404'), returnsNormally);
      expect(property.valid, false);
    });

    test('Creating a ByteDataProperty on an already deleted node is a no-op',
        () {
      late ByteDataProperty property;
      expect(() => property = deletedNode.byteDataProperty('404')!,
          returnsNormally);
      expect(() => property.setValue(toByteData('fuchsia')), returnsNormally);
      expect(property.valid, false);
    });
  });

  group('Effects of deletion include: ', () {
    Node? normalNode;

    setUp(() {
      normalNode = root.child('sheep');
    });

    test('child Node of deleted Node is deleted', () {
      var grandchild = normalNode!.child('goats')!;
      normalNode!.delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(grandchild.valid, false,
          reason: 'child Node of deleted Node should be deleted');
    });

    test('child IntProperty of deleted Node is deleted', () {
      var intProperty = normalNode!.intProperty('llamas')!;
      normalNode!.delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(intProperty.valid, false,
          reason: 'child IntProperty of deleted Node should be deleted');
    });

    test('child DoubleProperty of deleted Node is deleted', () {
      var doubleProperty = normalNode!.doubleProperty('emus')!;
      normalNode!.delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(doubleProperty.valid, false,
          reason: 'child DoubleProperty of deleted Node should be deleted');
    });

    test('child StringProperty of deleted Node is deleted', () {
      var stringProperty = normalNode!.stringProperty('okapis')!;
      normalNode!.delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(stringProperty.valid, false,
          reason: 'child StringProperty of deleted Node should be deleted');
    });

    test('child ByteDataProperty of deleted Node is deleted', () {
      var byteDataProperty = normalNode!.byteDataProperty('capybaras')!;
      normalNode!.delete();
      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
      expect(byteDataProperty.valid, false,
          reason: 'child ByteDataProperty of deleted Node should be deleted');
    });
  });

  group('VMO too small', () {
    Node? tinyRoot;
    setUp(() {
      var tinyVmo = FakeVmoHolder(64);
      var writer = VmoWriter.withVmo(tinyVmo);
      Inspect inspect = InspectImpl(writer);
      tinyRoot = inspect.root;
    });

    test('If no space, creation gives a deleted Node', () {
      var missingNode = tinyRoot!.child('missing')!;
      expect(() => missingNode.child('more missing'), returnsNormally);
      expect(missingNode.valid, false);
    });

    test('If no space, creation gives a deleted IntProperty', () {
      var missingProperty = tinyRoot!.intProperty('missing')!;
      expect(() => missingProperty.setValue(1), returnsNormally);
      expect(missingProperty.valid, false);
    });

    test('If no space, creation gives a deleted DoubleProperty', () {
      var missingProperty = tinyRoot!.doubleProperty('missing')!;
      expect(() => missingProperty.setValue(1.0), returnsNormally);
      expect(missingProperty.valid, false);
    });

    test('If no space, creation gives a deleted StringProperty', () {
      var missingProperty = tinyRoot!.stringProperty('missing')!;
      expect(() => missingProperty.setValue('something'), returnsNormally);
      expect(missingProperty.valid, false);
    });

    test('If no space, creation gives a deleted ByteDataProperty', () {
      var bytes = toByteData('this will not set');
      var missingProperty = tinyRoot!.byteDataProperty('missing')!;
      expect(() => missingProperty.setValue(bytes), returnsNormally);
      expect(missingProperty.valid, false);
    });
  });

  group('health', () {
    test('health statuses', () {
      const kNodeName = 'fuchsia.inspect.Health';

      final health = inspect.health;

      expect(VmoMatcher(vmo).node().at([kNodeName]), hasNoErrors);
      expect(
          VmoMatcher(vmo)
              .node()
              .at([kNodeName]).propertyEquals('status', 'STARTING_UP'),
          hasNoErrors);
      expect(
          VmoMatcher(vmo)
              .node()
              .at([kNodeName]).propertyEquals('start_timestamp_nanos', 42),
          hasNoErrors);
      expect(VmoMatcher(vmo).node().at([kNodeName])..missingChild('message'),
          hasNoErrors);

      health.setOk();
      expect(
          VmoMatcher(vmo).node().at([kNodeName]).propertyEquals('status', 'OK'),
          hasNoErrors);
      expect(VmoMatcher(vmo).node().at([kNodeName])..missingChild('message'),
          hasNoErrors);

      health.setStartingUp();
      expect(
          VmoMatcher(vmo)
              .node()
              .at([kNodeName]).propertyEquals('status', 'STARTING_UP'),
          hasNoErrors);
      expect(VmoMatcher(vmo).node().at([kNodeName])..missingChild('message'),
          hasNoErrors);

      health.setUnhealthy('Oh no');
      expect(
          VmoMatcher(vmo)
              .node()
              .at([kNodeName]).propertyEquals('status', 'UNHEALTHY'),
          hasNoErrors);
      expect(
          VmoMatcher(vmo)
              .node()
              .at([kNodeName]).propertyEquals('message', 'Oh no'),
          hasNoErrors);
    });
  });
}
