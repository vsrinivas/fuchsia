// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/entity/entity.dart';
import 'package:modular_core/entity/schema.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';

void main() {
  SchemaRegistry registry;

  setUp(() {
    registry = new SchemaRegistry();
  });
  group('builtin types', () {
    setUp(() {
      final Schema schema = new Schema('basicType', [
        new Property('int', 'int'),
        new Property('float', 'float'),
        new Property('string', 'string'),
        new Property('datetime', 'datetime'),
        new Property('int_repeated', 'int', isRepeated: true),
        new Property('float_repeated', 'float', isRepeated: true),
        new Property('string_repeated', 'string', isRepeated: true),
        new Property('datetime_repeated', 'datetime', isRepeated: true),
      ]);
      schema.publish(registry);
    });

    test('Get/set: single', () {
      final Entity entity = new Entity(['basicType'], registry: registry);

      // Singular
      expect(entity['int'], isNull);
      entity['int'] = 5;
      expect(entity['int'], equals(5));
      entity['int'] = null;
      expect(entity['int'], isNull);

      expect(entity['float'], isNull);
      entity['float'] = 1.5;
      expect(entity['float'], equals(1.5));
      entity['float'] = null;
      expect(entity['float'], isNull);

      expect(entity['string'], isNull);
      entity['string'] = 'hello';
      expect(entity['string'], equals('hello'));
      entity['string'] = null;
      expect(entity['string'], isNull);

      expect(entity['datetime'], isNull);
      final DateTime now = new DateTime.now();
      entity['datetime'] = now;
      expect(entity['datetime'], equals(now));
      entity['datetime'] = null;
      expect(entity['datetime'], isNull);
    });

    test('Get/set: repeated', () {
      final Entity entity = new Entity(['basicType'], registry: registry);

      final values = {
        'int_repeated': [1, (i) => i + 1],
        'float_repeated': [1.0, (i) => i + 0.5],
        'string_repeated': ['a', (i) => i + 'b'],
        'datetime_repeated': [
          new DateTime.now(),
          (i) => i.add(new Duration(days: 1))
        ],
      };

      // Test add() and removeAt().
      for (var key in values.keys) {
        final start = values[key][0];
        final incr = values[key][1];
        expect(entity[key].length, equals(0));

        var cur = start;
        for (int i = 0; i < 5; ++i) {
          entity[key].add(cur);

          for (int ii = 0; ii < i; ++ii) {
            expect(entity[key][ii], isNot(equals(cur)));
          }
          cur = incr(cur);
        }
        expect(entity[key].length, equals(5));

        expect(entity[key][0], equals(start));
        entity[key].removeAt(0);
        expect(entity[key][0], isNot(equals(start)));

        entity[key][0] = start;
        expect(entity[key][0], equals(start));
      }

      // Test clear().
      for (var key in values.keys) {
        entity[key].clear();
        expect(entity[key].length, equals(0));
      }

      // Test []=.
      for (var key in values.keys) {
        final start = values[key][0];
        final incr = values[key][1];

        entity[key] = [start, incr(start)];
        expect(entity[key].length, equals(2));
        expect(entity[key], equals([start, incr(start)]));
      }
    });

    test('validate', () {
      final Entity entity = new Entity(['basicType'], registry: registry);

      expect(() => entity['int'] = 10.0, throws);
      expect(() => entity['int'] = 'hi', throws);
      expect(() => entity['float'] = 'hi', throws);
      expect(() => entity['string'] = 1, throws);
      expect(() => entity['datetime'] = 1, throws);
      expect(() => entity['datetime'] = '5/10/14', throws);

      entity['int_repeated'].add(1);
      expect(() => entity['int_repeated'][0] = 1.5, throws);
      expect(() => entity['int_repeated'].add(1.5), throws);
      expect(() => entity['int_repeated'] = [2.5], throws);

      entity['float_repeated'].add(1.0);
      expect(() => entity['float_repeated'][0] = 'foo', throws);
      expect(() => entity['float_repeated'].add('bar'), throws);
      expect(() => entity['float_repeated'] = ['baz'], throws);

      entity['string_repeated'].add('hi');
      expect(() => entity['string_repeated'][0] = 5, throws);
      expect(() => entity['string_repeated'].add(1), throws);
      expect(() => entity['string_repeated'] = ['hi again', 8], throws);

      entity['datetime_repeated'].add(new DateTime.now());
      expect(() => entity['datetime_repeated'][0] = 5, throws);
      expect(() => entity['datetime_repeated'].add(1), throws);
      expect(() => entity['datetime_repeated'] = ['hi again', 8], throws);
    });
  });

  group('multiple types', () {
    test('set/get', () {
      final Schema schema1 =
          new Schema('type1', [new Property('type1_int', 'int')]);
      schema1.publish(registry);
      final Schema schema2 =
          new Schema('type2', [new Property('type2_int', 'int')]);
      schema2.publish(registry);

      final Entity entity = new Entity(['type1', 'type2'], registry: registry);

      entity['type1_int'] = 5;
      entity['type2_int'] = 10;

      expect(entity['type1_int'], equals(5));
      expect(entity['type2_int'], equals(10));
    });

    test('conflicting properties', () {
      final Schema schema1 = new Schema('type1', [new Property('int', 'int')]);
      schema1.publish(registry);
      final Schema schema2 = new Schema('type2', [new Property('int', 'int')]);
      schema2.publish(registry);

      final Entity entity = new Entity(['type1', 'type2'], registry: registry);
      entity['type1#int'] = 1;
      entity['type2#int'] = 2;

      expect(entity['int'], equals(1));

      entity['int'] = 10;
      expect(entity['type1#int'], equals(10));
      expect(entity['type2#int'], equals(2));
    });
  });

  group('entities as values', () {
    test('singly typed entities', () {
      final Schema schema1 =
          new Schema('type1', [new Property('thing', 'type2')]);
      schema1.publish(registry);
      final Schema schema2 = new Schema('type2', [new Property('int', 'int')]);
      schema2.publish(registry);

      final Entity e1 = new Entity(['type1'], registry: registry);
      final Entity e2 = new Entity(['type2'], registry: registry);

      e2['int'] = 10;
      e1['thing'] = e2;

      expect(e1['thing']['int'], equals(10));

      // When we set 'thing' to an entity of the wrong type, error.
      expect(() => e1['thing'] = e1, throws);
    });

    test('multiple-typed entities', () {
      final Schema schema1 = new Schema('type1',
          [new Property('thing2', 'type2'), new Property('thing3', 'type3')]);
      final Schema schema2 = new Schema('type2', [new Property('int1', 'int')]);
      final Schema schema3 = new Schema('type3', [new Property('int2', 'int')]);
      schema1.publish(registry);
      schema2.publish(registry);
      schema3.publish(registry);

      final Entity e1 = new Entity(['type1'], registry: registry);
      // e2 has multiple types, and it can be used to set a value of either one.
      final Entity e2 = new Entity(['type2', 'type3'], registry: registry);

      e2['int1'] = 10;
      e2['int2'] = 5;
      e1['thing2'] = e2;
      e1['thing3'] = e2;

      expect(e1['thing2']['int1'], equals(10));
      expect(e1['thing2']['int2'], equals(5));

      expect(e1['thing3']['int1'], equals(10));
      expect(e1['thing3']['int2'], equals(5));
    });
  });

  test('nonexistent properties', () {
    final Schema schema =
        new Schema('anotherType', [new Property('int', 'int')]);
    schema.publish();

    Entity entity = new Entity(['anotherType']);
    expect(() => entity['foobar'], throws);
    expect(() => entity['foobar'] = 5, throws);
  });

  test('nonexistent type', () {
    expect(() => new Entity(['i_dont_exist']), throws);
  });

  group('graph storage:', () {
    MemGraph graph;
    setUp(() {
      final Schema schema = new Schema('basicType', [
        new Property('int', 'int'),
        new Property('float', 'float'),
        new Property('string', 'string'),
        new Property('datetime', 'datetime'),
        new Property('int_repeated', 'int', isRepeated: true),
        new Property('float_repeated', 'float', isRepeated: true),
        new Property('string_repeated', 'string', isRepeated: true),
        new Property('datetime_repeated', 'datetime', isRepeated: true),
      ]);
      schema.publish(registry);

      graph = new MemGraph();
    });

    test('Entity node', () {
      Entity entity = new Entity(['basicType'], registry: registry);
      expect(entity.node, isNull);
      expect(entity.id, isNull);
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(entity.node, isNotNull);
      expect(entity.id, isNotNull);
      expect(graph.nodes.length, equals(1));
      expect(graph.edges.length, equals(0));
      expect(graph.nodes.first, equals(entity.node));

      // Save it again, and we shouldn't see any new nodes.
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(1));

      // Load a new entity from the same node.
      Entity sameEntity = new Entity.fromNode(entity.node, registry: registry);
      expect(sameEntity.types, equals(['basicType']));

      // Now delete the entity and see that the node goes away.
      graph.mutate((GraphMutator mutator) {
        entity.delete(mutator);
      });
      expect(graph.nodes.length, equals(0));
    });

    test('scalar values', () {
      Entity entity = new Entity(['basicType'], registry: registry);
      entity['int'] = 1;
      entity['float'] = 2.0;
      entity['string'] = 'string';
      entity['datetime'] = new DateTime.now();
      entity['int_repeated'] = [2, 3];
      entity['float_repeated'] = [3.0, 4.0];
      entity['string_repeated'] = ['hello', 'world'];
      entity['datetime_repeated'] = [new DateTime(1999), new DateTime(2000)];

      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      int numNodes = graph.nodes.length;
      expect(numNodes, greaterThan(2));

      // Save it again, and we shouldn't see any new nodes.
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(numNodes));

      // Load a new entity from the same node.
      Entity sameEntity = new Entity.fromNode(entity.node, registry: registry);
      expect(sameEntity.types, equals(['basicType']));
      expect(sameEntity['int'], entity['int']);
      expect(sameEntity['float'], entity['float']);
      expect(sameEntity['string'], entity['string']);
      expect(sameEntity['datetime'], entity['datetime']);
      expect(sameEntity['int_repeated'], entity['int_repeated']);
      expect(sameEntity['float_repeated'], entity['float_repeated']);
      expect(sameEntity['string_repeated'], entity['string_repeated']);
      expect(sameEntity['datetime_repeated'], entity['datetime_repeated']);

      // Delete just one value.
      entity['int'] = null;
      // Save it again. See that just the one node is gone.
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(numNodes - 1));

      graph.mutate((GraphMutator mutator) {
        entity.delete(mutator);
      });
      expect(graph.nodes.length, equals(0));
      expect(graph.edges.length, equals(0));
    });

    test('scalar values: repeated field updates', () {
      // When mutating a single repeated field, we should see nodes and edges
      // added *and deleted* when making those updates.
      Entity entity = new Entity(['basicType'], registry: registry);

      entity['int_repeated'] = [1];
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(2));
      expect(graph.edges.length, equals(1));

      entity['int_repeated'] = [2];
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(2));
      expect(graph.edges.length, equals(1));
      expect(
          new Entity.fromNode(entity.node, registry: registry)['int_repeated'],
          equals([2]));

      entity['int_repeated'] = [2, 3];
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(3));
      expect(graph.edges.length, equals(2));
      expect(
          new Entity.fromNode(entity.node, registry: registry)['int_repeated'],
          equals([2, 3]));

      entity['int_repeated'] = [5];
      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });
      expect(graph.nodes.length, equals(2));
      expect(graph.edges.length, equals(1));
      expect(
          new Entity.fromNode(entity.node, registry: registry)['int_repeated'],
          equals([5]));

      graph.mutate((GraphMutator mutator) {
        entity.delete(mutator);
      });
      expect(graph.nodes.length, equals(0));
      expect(graph.edges.length, equals(0));
    });

    test('reload', () {
      final entity = new Entity(['basicType'], registry: registry);
      entity['int'] = 10;

      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });

      final sameEntity = new Entity.fromNode(entity.node, registry: registry);
      sameEntity['int'] = 20;
      graph.mutate((GraphMutator mutator) {
        sameEntity.save(mutator);
      });

      expect(entity['int'], equals(10));
      entity.reload();
      expect(entity['int'], equals(20));

      // Do the same thing for repeated values.
      entity['int_repeated'] = [1, 2];

      graph.mutate((GraphMutator mutator) {
        entity.save(mutator);
      });

      sameEntity.reload();
      sameEntity['int_repeated'] = [5];
      graph.mutate((GraphMutator mutator) {
        sameEntity.save(mutator);
      });

      expect(entity['int_repeated'], equals([1, 2]));
      entity.reload();
      expect(entity['int_repeated'], equals([5]));
    });

    group('entity values:', () {
      setUp(() {
        new Schema('type1', [
          new Property('property', 'type2'),
          new Property.int('int')
        ]).publish(registry);
        new Schema('type2', [
          new Property('property', 'type1'),
          new Property.int('int')
        ]).publish(registry);
      });

      test('basic', () {
        final entity1 = new Entity(['type1'], registry: registry);
        final entity2 = new Entity(['type2'], registry: registry);

        entity1['property'] = entity2;

        graph.mutate((GraphMutator mutator) {
          entity1.save(mutator);
        });

        expect(
            new Entity.fromNode(entity1.node, registry: registry)['property']
                .id,
            equals(entity2.id));

        // We should still see entity2 in the graph.
        graph.mutate((GraphMutator mutator) {
          entity1.delete(mutator);
        });
        expect(graph.nodes.length, equals(1));
        expect(graph.edges.length, equals(0));
      });

      test('circular', () {
        final entity1 = new Entity(['type1'], registry: registry);
        final entity2 = new Entity(['type2'], registry: registry);

        entity1['property'] = entity2;
        entity2['property'] = entity1;

        graph.mutate((GraphMutator mutator) {
          entity1.save(mutator);
        });

        expect(
            new Entity.fromNode(entity1.node, registry: registry)['property']
                .id,
            equals(entity2.id));

        expect(
            new Entity.fromNode(entity2.node, registry: registry)['property']
                .id,
            equals(entity1.id));
      });

      test('reload', () {
        final entity1 = new Entity(['type1'], registry: registry);
        entity1['property'] = new Entity(['type2'], registry: registry);

        graph.mutate((GraphMutator mutator) {
          entity1.save(mutator);
        });
        final sameEntity1 =
            new Entity.fromNode(entity1.node, registry: registry);
        sameEntity1['property']['int'] = 10;

        graph.mutate((GraphMutator mutator) {
          sameEntity1['property'].save(mutator);
        });
        expect(entity1['property']['int'], isNull);
        entity1.reload();
        expect(entity1['property']['int'], equals(10));
      });
    });
  });
}
