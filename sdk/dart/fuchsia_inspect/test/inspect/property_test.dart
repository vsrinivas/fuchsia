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
  late Node node;

  setUp(() {
    vmo = FakeVmoHolder(512);
    var writer = VmoWriter.withVmo(vmo);
    Inspect inspect = InspectImpl(writer);
    node = inspect.root!;
  });

  group('String properties', () {
    test('are written to the VMO when the value is set', () {
      var _ = node.stringProperty('color')!..setValue('fuchsia');

      expect(VmoMatcher(vmo).node()..propertyEquals('color', 'fuchsia'),
          hasNoErrors);
    });

    test('can be mutated', () {
      var property = node.stringProperty('breakfast')!..setValue('pancakes');

      expect(VmoMatcher(vmo).node()..propertyEquals('breakfast', 'pancakes'),
          hasNoErrors);

      property.setValue('waffles');
      expect(VmoMatcher(vmo).node()..propertyEquals('breakfast', 'waffles'),
          hasNoErrors);
    });

    test('can be deleted', () {
      var property = node.stringProperty('scallops')!;
      expect(VmoMatcher(vmo).node().property('scallops'), hasNoErrors);

      property.delete();

      expect(VmoMatcher(vmo).node()..missingChild('scallops'), hasNoErrors);
    });

    test('setting a value on an already deleted property is a no-op', () {
      var property = node.stringProperty('paella')!;
      expect(VmoMatcher(vmo).node().property('paella'), hasNoErrors);
      property.delete();

      expect(() => property.setValue('this will not set'), returnsNormally);
      expect(VmoMatcher(vmo).node()..missingChild('paella'), hasNoErrors);
    });

    test('deleted property is a no-op', () {
      final property = StringProperty.deleted();
      expect(property.valid, false);
    });

    test('removing an already deleted property is a no-op', () {
      var property = node.stringProperty('nothing-here')!..delete();

      expect(() => property.delete(), returnsNormally);
    });
  });

  group('ByteData properties', () {
    test('are written to the VMO when the property is set', () {
      var bytes = toByteData('fuchsia');
      var _ = node.byteDataProperty('color')!..setValue(bytes);

      expect(
          VmoMatcher(vmo).node()
            ..propertyEquals('color', bytes.buffer.asUint8List()),
          hasNoErrors);
    });

    test('can be mutated', () {
      var pancakes = toByteData('pancakes');
      var property = node.byteDataProperty('breakfast')!..setValue(pancakes);

      expect(
          VmoMatcher(vmo).node()
            ..propertyEquals('breakfast', pancakes.buffer.asUint8List()),
          hasNoErrors);

      var waffles = toByteData('waffles');
      property.setValue(waffles);
      expect(
          VmoMatcher(vmo).node()
            ..propertyEquals('breakfast', waffles.buffer.asUint8List()),
          hasNoErrors);
    });

    test('can be deleted', () {
      var property = node.byteDataProperty('scallops')!;
      expect(VmoMatcher(vmo).node().property('scallops'), hasNoErrors);

      property.delete();

      expect(VmoMatcher(vmo).node()..missingChild('scallops'), hasNoErrors);
    });

    test('setting a value on an already deleted property is a no-op', () {
      var property = node.byteDataProperty('paella')!;
      expect(VmoMatcher(vmo).node().property('paella'), hasNoErrors);
      property.delete();

      var bytes = toByteData('this will not set');
      expect(() => property.setValue(bytes), returnsNormally);
      expect(VmoMatcher(vmo).node()..missingChild('paella'), hasNoErrors);
    });

    test('removing an already deleted property is a no-op', () {
      var property = node.byteDataProperty('nothing-here')!..delete();

      expect(() => property.delete(), returnsNormally);
    });
  });

  group('Property creation (byte-vector properties)', () {
    test('StringProperties created twice return the same object', () {
      var childProperty = node.stringProperty('banana');
      var childProperty2 = node.stringProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, same(childProperty2));
    });

    test('StringProperties created after deletion return different objects',
        () {
      var childProperty = node.stringProperty('banana')!..delete();
      var childProperty2 = node.stringProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, isNot(equals(childProperty2)));
    });

    test('ByteDataProperties created twice return the same object', () {
      var childProperty = node.byteDataProperty('banana');
      var childProperty2 = node.byteDataProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, same(childProperty2));
    });

    test('ByteDataProperties created after deletion return different objects',
        () {
      var childProperty = node.byteDataProperty('banana')!..delete();
      var childProperty2 = node.byteDataProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, isNot(equals(childProperty2)));
    });

    test('Changing StringProperty to ByteDataProperty throws', () {
      node.stringProperty('banana');
      expect(() => node.byteDataProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing StringProperty to IntProperty throws', () {
      node.stringProperty('banana');
      expect(() => node.intProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing StringProperty to DoubleProperty throws', () {
      node.stringProperty('banana');
      expect(() => node.doubleProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing ByteDataProperty to StringProperty throws', () {
      node.byteDataProperty('banana');
      expect(() => node.stringProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing ByteDataProperty to IntProperty throws', () {
      node.byteDataProperty('banana');
      expect(() => node.intProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing ByteDataProperty to DoubleProperty throws', () {
      node.byteDataProperty('banana');
      expect(() => node.doubleProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('If no space, creation gives a deleted StringProperty', () {
      var tinyVmo = FakeVmoHolder(64);
      var writer = VmoWriter.withVmo(tinyVmo);
      Inspect inspect = InspectImpl(writer);
      var tinyRoot = inspect.root!;
      var missingProperty = tinyRoot.stringProperty('missing');
      expect(() => missingProperty!.setValue('something'), returnsNormally);
      expect(VmoMatcher(tinyVmo).node()..missingChild('missing'), hasNoErrors);
    });

    test('If no space, creation gives a deleted ByteDataProperty', () {
      var tinyVmo = FakeVmoHolder(64);
      var writer = VmoWriter.withVmo(tinyVmo);
      Inspect inspect = InspectImpl(writer);
      var tinyRoot = inspect.root!;
      var bytes = toByteData('this will not set');
      var missingProperty = tinyRoot.byteDataProperty('missing');
      expect(() => missingProperty!.setValue(bytes), returnsNormally);
      expect(VmoMatcher(tinyVmo).node()..missingChild('missing'), hasNoErrors);
    });
  });

  group('Int Properties', () {
    test('are created with value 0', () {
      var _ = node.intProperty('foo');

      expect(VmoMatcher(vmo).node().propertyEquals('foo', 0), hasNoErrors);
    });

    test('are written to the VMO when the value is set', () {
      var _ = node.intProperty('eggs')!..setValue(12);

      expect(VmoMatcher(vmo).node().propertyEquals('eggs', 12), hasNoErrors);
    });

    test('can be mutated', () {
      var property = node.intProperty('locusts')!..setValue(10);
      expect(VmoMatcher(vmo).node().propertyEquals('locusts', 10), hasNoErrors);

      property.setValue(1000);

      expect(
          VmoMatcher(vmo).node().propertyEquals('locusts', 1000), hasNoErrors);
    });

    test('can add arbitrary values', () {
      var property = node.intProperty('bagels')!..setValue(13);
      expect(VmoMatcher(vmo).node().propertyEquals('bagels', 13), hasNoErrors);

      property.add(13);

      expect(VmoMatcher(vmo).node().propertyEquals('bagels', 26), hasNoErrors);
    });

    test('can subtract arbitrary values', () {
      var property = node.intProperty('bagels')!..setValue(13);
      expect(VmoMatcher(vmo).node().propertyEquals('bagels', 13), hasNoErrors);

      property.subtract(6);

      expect(VmoMatcher(vmo).node().propertyEquals('bagels', 7), hasNoErrors);
    });

    test('can be deleted', () {
      var _ = node.intProperty('sheep')!..delete();

      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
    });

    test('setting a value on an already deleted property is a no-op', () {
      var property = node.intProperty('webpages')!..delete();

      expect(() => property.setValue(404), returnsNormally);
      expect(VmoMatcher(vmo).node()..missingChild('webpages'), hasNoErrors);
    });

    test('removing an already deleted property is a no-op', () {
      var property = node.intProperty('nothing-here')!..delete();

      expect(() => property.delete(), returnsNormally);
    });
  });

  group('Bool Properties', () {
    test('are created with value false', () {
      var _ = node.boolProperty('foo');

      expect(VmoMatcher(vmo).node().propertyEquals('foo', false), hasNoErrors);
    });

    test('are written to the VMO when the value is set', () {
      var _ = node.boolProperty('eggs')!..setValue(true);

      expect(VmoMatcher(vmo).node().propertyEquals('eggs', true), hasNoErrors);
    });

    test('can be mutated', () {
      var property = node.boolProperty('locusts')!..setValue(true);
      expect(
          VmoMatcher(vmo).node().propertyEquals('locusts', true), hasNoErrors);

      property.setValue(false);

      expect(
          VmoMatcher(vmo).node().propertyEquals('locusts', false), hasNoErrors);
    });

    test('can be deleted', () {
      var _ = node.boolProperty('sheep')!..delete();

      expect(VmoMatcher(vmo).node()..missingChild('sheep'), hasNoErrors);
    });

    test('setting a value on an already deleted property is a no-op', () {
      var property = node.boolProperty('webpages')!..delete();

      expect(() => property.setValue(false), returnsNormally);
      expect(VmoMatcher(vmo).node()..missingChild('webpages'), hasNoErrors);
    });

    test('removing an already deleted property is a no-op', () {
      var property = node.boolProperty('nothing-here')!..delete();

      expect(() => property.delete(), returnsNormally);
    });
  });

  group('DoubleProperties', () {
    test('are created with value 0', () {
      var _ = node.doubleProperty('foo');

      expect(VmoMatcher(vmo).node().propertyEquals('foo', 0.0), hasNoErrors);
    });

    test('are written to the VMO when the value is set', () {
      var _ = node.doubleProperty('foo')!..setValue(2.5);

      expect(VmoMatcher(vmo).node().propertyEquals('foo', 2.5), hasNoErrors);
    });

    test('can be mutated', () {
      var property = node.doubleProperty('bar')!..setValue(3.0);
      expect(VmoMatcher(vmo).node().propertyEquals('bar', 3.0), hasNoErrors);

      property.setValue(3.5);

      expect(VmoMatcher(vmo).node().propertyEquals('bar', 3.5), hasNoErrors);
    });

    test('can add arbitrary values', () {
      var property = node.doubleProperty('cake')!..setValue(1.5);
      expect(VmoMatcher(vmo).node().propertyEquals('cake', 1.5), hasNoErrors);

      property.add(1.5);

      expect(VmoMatcher(vmo).node().propertyEquals('cake', 3.0), hasNoErrors);
    });

    test('can subtract arbitrary values', () {
      var property = node.doubleProperty('cake')!..setValue(5);
      expect(VmoMatcher(vmo).node().propertyEquals('cake', 5.0), hasNoErrors);

      property.subtract(0.5);

      expect(VmoMatcher(vmo).node().propertyEquals('cake', 4.5), hasNoErrors);
    });

    test('can be deleted', () {
      var _ = node.doubleProperty('circumference')!..delete();

      expect(
          VmoMatcher(vmo).node()..missingChild('circumference'), hasNoErrors);
    });

    test('setting a value on an already deleted property is a no-op', () {
      var property = node.doubleProperty('pounds')!..delete();

      expect(() => property.setValue(50.6), returnsNormally);
      expect(VmoMatcher(vmo).node()..missingChild('pounds'), hasNoErrors);
    });

    test('removing an already deleted property is a no-op', () {
      var property = node.doubleProperty('nothing-here')!..delete();

      expect(() => property.delete(), returnsNormally);
    });
  });

  group('property creation', () {
    test('IntProperties created twice return the same object', () {
      var childProperty = node.intProperty('banana');
      var childProperty2 = node.intProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, same(childProperty2));
    });

    test('IntProperties created after deletion return different objects', () {
      var childProperty = node.intProperty('banana')!..delete();
      var childProperty2 = node.intProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, isNot(equals(childProperty2)));
    });

    test('DoubleProperties created twice return the same object', () {
      var childProperty = node.doubleProperty('banana');
      var childProperty2 = node.doubleProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, same(childProperty2));
    });

    test('DoubleProperties created after deletion return different objects',
        () {
      var childProperty = node.doubleProperty('banana')!..delete();
      var childProperty2 = node.doubleProperty('banana');

      expect(childProperty, isNotNull);
      expect(childProperty2, isNotNull);
      expect(childProperty, isNot(equals(childProperty2)));
    });

    test('Changing IntProperty to DoubleProperty throws', () {
      node.intProperty('banana');
      expect(() => node.doubleProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing IntProperty to StringProperty throws', () {
      node.intProperty('banana');
      expect(() => node.stringProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing IntProperty to ByteDataProperty throws', () {
      node.intProperty('banana');
      expect(() => node.byteDataProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing DoubleProperty to IntProperty throws', () {
      node.doubleProperty('banana');
      expect(() => node.intProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing DoubleProperty to StringProperty throws', () {
      node.doubleProperty('banana');
      expect(() => node.stringProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('Changing DoubleProperty to ByteDataProperty throws', () {
      node.doubleProperty('banana');
      expect(() => node.byteDataProperty('banana'),
          throwsA(const TypeMatcher<InspectStateError>()));
    });

    test('If no space, creation gives a deleted IntProperty', () {
      var tinyVmo = FakeVmoHolder(64);
      var writer = VmoWriter.withVmo(tinyVmo);
      Inspect inspect = InspectImpl(writer);
      var tinyRoot = inspect.root!;
      var missingProperty = tinyRoot.intProperty('missing');
      expect(() => missingProperty!.setValue(1), returnsNormally);
      expect(VmoMatcher(tinyVmo).node()..missingChild('missing'), hasNoErrors);
    });

    test('If no space, creation gives a deleted DoubleProperty', () {
      var tinyVmo = FakeVmoHolder(64);
      var writer = VmoWriter.withVmo(tinyVmo);
      Inspect inspect = InspectImpl(writer);
      var tinyRoot = inspect.root!;
      var missingProperty = tinyRoot.doubleProperty('missing');
      expect(() => missingProperty!.setValue(1.0), returnsNormally);
      expect(VmoMatcher(tinyVmo).node()..missingChild('missing'), hasNoErrors);
    });
  });

  test('Able to call InspectImpl at a specified path', () {
    var tinyVmo = FakeVmoHolder(64);
    var tinyVmo2 = FakeVmoHolder(64);
    var writer = VmoWriter.withVmo(tinyVmo);
    var writer2 = VmoWriter.withVmo(tinyVmo2);
    Inspect inspect = InspectImpl(writer);
    Inspect inspect2 = InspectImpl(writer2, fileNamePrefix: 'test');
    expect(() => inspect, isNotNull);
    expect(() => inspect2, isNotNull);
  });
}
