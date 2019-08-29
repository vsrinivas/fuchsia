// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// import 'dart:convert';

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mondrian/models/surface/surface.dart';
import 'package:mondrian/models/surface/surface_graph.dart';
import 'package:mondrian/models/surface/surface_properties.dart';
import 'package:mondrian/models/tree/spanning_tree.dart';
import 'package:mondrian/models/tree/tree.dart';

void main() {
  SurfaceGraph graph;
  SurfaceProperties properties = SurfaceProperties();
  SurfaceRelation depcop = SurfaceRelation(
    emphasis: 0.12,
    arrangement: SurfaceArrangement.copresent,
    dependency: SurfaceDependency.dependent,
  );
  SurfaceRelation cop =
      SurfaceRelation(arrangement: SurfaceArrangement.copresent);
  SurfaceRelation unrelated =
      SurfaceRelation(arrangement: SurfaceArrangement.sequential);

  setUp(() {
    graph = SurfaceGraph();
  });

  test('getCopresentSpanningTree with one surface in the graph', () {
    Surface parent =
        graph.addSurface('value', properties, '', depcop, null, '');
    Tree<Surface> spanningTree = getCopresentSpanningTree(parent);
    expect(spanningTree.length, 1);
    expect(spanningTree.value, parent);
  });

  test('getCopresentSpanningTree from grandchild with 3 surfaces in the graph',
      () {
    Surface parent =
        graph.addSurface('parent', properties, '', depcop, null, '');
    Surface child =
        graph.addSurface('child', properties, 'parent', depcop, null, '');
    Surface grandchild =
        graph.addSurface('grandchild', properties, 'child', depcop, null, '');
    Tree<Surface> spanningTree = getCopresentSpanningTree(grandchild);

    expect(spanningTree.length, 3);
    expect(spanningTree.value, grandchild);
    List<Surface> children =
        spanningTree.map((Tree<Surface> node) => node.value).toList();
    expect(children.contains(child), true);
    expect(children.contains(parent), true);
  });

  test(
      'getCopresentSpanningTree from grandchild with 2 other unrelated surfaces in the graph',
      () {
    graph
      ..addSurface('a', properties, '', depcop, null, '')
      ..addSurface('b', properties, '', depcop, null, '');
    Surface grandchild =
        graph.addSurface('c', properties, '', depcop, null, '');
    Tree<Surface> spanningTree = getCopresentSpanningTree(grandchild);

    expect(spanningTree.length, 1);
    expect(spanningTree.value, grandchild);
  });

  test(
      'getDependentSpanningTree from node with 2 unrelated surfaces in the graph',
      () {
    graph
      ..addSurface('a', properties, '', depcop, null, '')
      ..addSurface('b', properties, '', depcop, null, '');
    Surface grandchild =
        graph.addSurface('c', properties, '', depcop, null, '');
    Tree<Surface> spanningTree = getDependentSpanningTree(grandchild);

    expect(spanningTree.length, 1);
    expect(spanningTree.value, grandchild);
  });

  test(
      'getDependentSpanningTree from grandchild with 2 related surfaces in the graph',
      () {
    graph
      ..addSurface('parent', properties, '', depcop, null, '')
      ..addSurface('child', properties, 'parent', depcop, null, '')
      ..addSurface('unrelated', properties, 'child', unrelated, null, '');
    Surface grandchild =
        graph.addSurface('grandchild', properties, 'child', depcop, null, '');
    Tree<Surface> spanningTree = getDependentSpanningTree(grandchild);

    expect(spanningTree.length, 3);
  });

  test('getDependentSpanningTrees with 1 tree', () {
    graph
      ..addSurface('parent', properties, '', depcop, null, '')
      ..addSurface('child', properties, 'parent', depcop, null, '');
    Surface grandchild =
        graph.addSurface('grandchild', properties, 'child', depcop, null, '');
    List<Tree<Surface>> spanningTrees = getDependentSpanningTrees(grandchild);
    expect(spanningTrees.length, 1);
  });

  test('getDependentSpanningTrees with 2 trees', () {
    Surface firstRoot =
        graph.addSurface('firstRoot', properties, '', depcop, null, '');
    graph
      ..addSurface('child', properties, 'firstRoot', depcop, null, '')
      ..addSurface('grandchild', properties, 'child', depcop, null, '')
      ..addSurface('secondRoot', properties, 'firstRoot', cop, null, '')
      ..addSurface('secondChild', properties, 'secondRoot', depcop, null, '');
    List<Tree<Surface>> spanningTrees = getDependentSpanningTrees(firstRoot);
    expect(spanningTrees.length, 2);
  });
}
