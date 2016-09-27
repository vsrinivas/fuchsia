// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';

import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/mutation_list.dart';

void main() {
  test('mutationsWith/WithoutTags', () {
    GraphMutationList list = new GraphMutationList.from([
      new GraphMutation(GraphMutationType.addNode,
          nodeId: new NodeId.fromString('foo'),
          tags: new Set<dynamic>.from(['foo', 'baz'])),
      new GraphMutation(GraphMutationType.addNode,
          nodeId: new NodeId.fromString('foo'),
          tags: new Set<dynamic>.from(['bar', 'baz']))
    ]);

    expect(list.withTag('baz'), equals(list.toList()));
    expect(list.withTag('bar'), equals([list.toList()[1]]));
    expect(list.withTag('foo'), equals([list.toList()[0]]));

    expect(list.withoutTag('baz'), equals([]));
    expect(list.withoutTag('bar'), equals([list.toList()[0]]));
    expect(list.withoutTag('foo'), equals([list.toList()[1]]));
  });

  test('coalesced', () {
    // Coalescing setValues.
    GraphMutationList list = new GraphMutationList.from([
      new GraphMutation.setValue(
              new NodeId.fromString('2'), 'key', new Uint8List.fromList([1, 2]))
          .withTag('foo'),
      new GraphMutation.setValue(
              new NodeId.fromString('2'), 'key', new Uint8List.fromList([2, 3]))
          .withTag('bar'),
    ]);

    expect(
        list.coalesced,
        equals([
          new GraphMutation.setValue(new NodeId.fromString('2'), 'key',
              new Uint8List.fromList([2, 3])).withTag('bar')
        ]));

    // Coalescing add/remove.
    list = new GraphMutationList.from([
      new GraphMutation.addNode(new NodeId.fromString('0')),
      new GraphMutation.addEdge(new EdgeId.fromString('1'),
          new NodeId.fromString('1'), new NodeId.fromString('1'), []),
      new GraphMutation.setValue(
          new NodeId.fromString('2'), 'key', new Uint8List.fromList([1, 2])),
      new GraphMutation.setValue(new NodeId.fromString('2'), 'key', null),
      new GraphMutation.removeEdge(new EdgeId.fromString('1'),
          new NodeId.fromString('1'), new NodeId.fromString('1'), []),
      new GraphMutation.removeNode(new NodeId.fromString('0'))
    ]);

    expect(
        list.coalesced,
        equals([
          new GraphMutation.setValue(new NodeId.fromString('2'), 'key', null)
        ]));
  });
}
