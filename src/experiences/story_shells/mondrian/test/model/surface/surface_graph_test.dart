// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mondrian/models/surface/surface.dart';
import 'package:mondrian/models/surface/surface_graph.dart';
import 'package:mondrian/models/surface/surface_properties.dart';
import 'package:zircon/zircon.dart';

void main() {
  test('toJson and back again with a single surface', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('value', properties, '', relation, null, '')
      ..connectView('value', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value');
    expect(graph.focusStack.length, 1);
    String encoded = json.encode(graph);

    Map<String, dynamic> decoded = json.decode(encoded);
    SurfaceGraph decodedGraph = SurfaceGraph.fromJson(decoded);

    expect(decodedGraph.focusStack.length, 1);
    Surface surface = decodedGraph.focusStack.first;
    expect(surface.node.value, 'value');
    expect(surface.parent, null);
    expect(surface.relation.arrangement, SurfaceArrangement.copresent);
    expect(surface.relation.dependency, SurfaceDependency.dependent);
    expect(surface.relation.emphasis, 0.12);
    expect(surface.properties.containerLabel, 'containerLabel');
  });

  test('toJson and back again with two surfaces', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('parent', properties, '', relation, null, '')
      ..connectView('parent', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('parent');
    expect(graph.focusStack.length, 1);

    properties = SurfaceProperties(containerLabel: 'containerLabel');
    relation = SurfaceRelation(
      emphasis: 0.5,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('child', properties, 'parent', relation, null, '')
      ..connectView('child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('child');
    expect(graph.focusStack.length, 2);

    String encoded = json.encode(graph);

    Map<String, dynamic> decoded = json.decode(encoded);
    SurfaceGraph decodedGraph = SurfaceGraph.fromJson(decoded);

    expect(decodedGraph.focusStack.length, 2);
    Surface surface = decodedGraph.focusStack.first;
    expect(surface.node.value, 'parent');
    expect(surface.node.parent.value, null);

    // expect(surface.parentId, null);
    expect(surface.relation.arrangement, SurfaceArrangement.copresent);
    expect(surface.relation.dependency, SurfaceDependency.dependent);
    expect(surface.relation.emphasis, 0.12);
    expect(surface.properties.containerLabel, 'containerLabel');
    expect(surface.children.length, 1);
    expect(surface.children.first.node.value, 'child');

    Surface secondSurface = decodedGraph.focusStack.last;
    expect(secondSurface.node.value, 'child');
    expect(secondSurface.parentId, 'parent');
    expect(secondSurface.relation.arrangement, SurfaceArrangement.copresent);
    expect(secondSurface.relation.dependency, SurfaceDependency.dependent);
    expect(secondSurface.relation.emphasis, 0.5);
    expect(secondSurface.properties.containerLabel, 'containerLabel');
  });

  test('toJson and back again with one surface with two children', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('parent', properties, '', relation, null, '')
      ..connectView('parent', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('parent');
    expect(graph.focusStack.length, 1);

    properties = SurfaceProperties(containerLabel: 'containerLabel');
    relation = SurfaceRelation(
      emphasis: 0.5,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('child1', properties, 'parent', relation, null, '')
      ..connectView('child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('child1');
    expect(graph.focusStack.length, 2);

    properties = SurfaceProperties(containerLabel: 'containerLabel');
    relation = SurfaceRelation(
      emphasis: 0.0,
      arrangement: SurfaceArrangement.ontop,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('child2', properties, 'parent', relation, null, '')
      ..connectView('child2', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('child2');
    expect(graph.focusStack.length, 3);

    String encoded = json.encode(graph);

    Map<String, dynamic> decoded = json.decode(encoded);
    SurfaceGraph decodedGraph = SurfaceGraph.fromJson(decoded);

    expect(decodedGraph.focusStack.length, 3);
    Surface surface = decodedGraph.focusStack.first;
    expect(surface.node.value, 'parent');
    expect(surface.node.parent.value, null);
    expect(surface.relation.arrangement, SurfaceArrangement.copresent);
    expect(surface.relation.dependency, SurfaceDependency.dependent);
    expect(surface.relation.emphasis, 0.12);
    expect(surface.properties.containerLabel, 'containerLabel');
    expect(surface.children.length, 2);
    List<String> children = [];
    for (Surface surface in surface.children) {
      children.add(surface.node.value);
    }
    expect(children.first, 'child1');
    expect(children.last, 'child2');

    Surface secondSurface = decodedGraph.focusStack.toList()[1];
    expect(secondSurface.node.value, 'child1');
    expect(secondSurface.parentId, 'parent');
    expect(secondSurface.relation.arrangement, SurfaceArrangement.copresent);
    expect(secondSurface.relation.dependency, SurfaceDependency.dependent);
    expect(secondSurface.relation.emphasis, 0.5);
    expect(secondSurface.properties.containerLabel, 'containerLabel');

    Surface thirdSurface = decodedGraph.focusStack.last;
    expect(thirdSurface.node.value, 'child2');
    expect(thirdSurface.parentId, 'parent');
    expect(thirdSurface.relation.arrangement, SurfaceArrangement.ontop);
    expect(thirdSurface.relation.dependency, SurfaceDependency.dependent);
    expect(thirdSurface.relation.emphasis, 0.0);
    expect(thirdSurface.properties.containerLabel, 'containerLabel');
  });

  test('external surfaces are found by resummon dismissed checks', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties externalProp =
        SurfaceProperties(source: ModuleSource.external);
    graph
      ..addSurface(
          'parent', SurfaceProperties(), '', SurfaceRelation(), null, '')
      ..connectView('parent', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('parent')
      // Now add external surface
      ..addSurface(
          'external', externalProp, 'parent', SurfaceRelation(), null, '')
      ..connectView('external', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('external')
      // Now dismiss the external surface
      ..dismissSurface('external');
    // expect that there is a dismissed external associated with the parent
    expect(graph.externalSurfaces(surfaceId: 'parent'), ['external']);
  });

  test('duplicate surface add', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('value', properties, '', relation, null, '')
      ..connectView('value', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value');
    expect(graph.treeSize, 2);

    graph
      ..addSurface('value', properties, '', relation, null, '')
      ..connectView('value', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value');
    expect(graph.treeSize, 2);
  });

  test('duplicate child surface add', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('value', properties, '', relation, null, '')
      ..connectView('value', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value');
    expect(graph.treeSize, 2);

    graph
      ..addSurface('value.child', properties, '', relation, null, '')
      ..connectView('value.child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value.child');
    expect(graph.treeSize, 3);

    graph
      ..addSurface('value.child', properties, '', relation, null, '')
      ..connectView('value.child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value.child');
    expect(graph.treeSize, 3);
  });

  test('duplicate child surface add', () {
    SurfaceGraph graph = SurfaceGraph();
    SurfaceProperties properties =
        SurfaceProperties(containerLabel: 'containerLabel');
    SurfaceRelation relation = SurfaceRelation(
      emphasis: 0.12,
      arrangement: SurfaceArrangement.copresent,
      dependency: SurfaceDependency.dependent,
    );
    graph
      ..addSurface('value', properties, '', relation, null, '')
      ..connectView('value', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value');
    expect(graph.treeSize, 2);

    graph
      ..addSurface('value.child', properties, '', relation, null, '')
      ..connectView('value.child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value.child');
    expect(graph.treeSize, 3);

    graph
      ..addSurface('value.child', properties, '', relation, null, '')
      ..connectView('value.child', ViewHolderToken(value: EventPair(null)))
      ..focusSurface('value.child');
    expect(graph.treeSize, 3);
  });
}
