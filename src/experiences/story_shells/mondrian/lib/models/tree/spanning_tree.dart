// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import '../surface/surface.dart';
import '../surface/surface_graph.dart';
import 'tree.dart';

/// Provides methods for finding spanning trees

// Creates a spanning tree with a given condition
Tree<Surface> _spanningTree(
    Surface previous, Surface current, bool condition(Surface s)) {
  assert(current != null);
  Tree<Surface> tree = Tree<Surface>(value: current);
  if (current.parent != previous &&
      current.parent != null &&
      condition(current)) {
    tree.add(_spanningTree(current, current.parent, condition));
  }
  for (Surface child in current.children) {
    if (child != previous && condition(child)) {
      tree.add(
        _spanningTree(current, child, condition),
      );
    }
  }
  return tree;
}

/// Gets the dependent spanning tree the current widget is part of
Tree<Surface> getDependentSpanningTree(Surface surface) {
  Tree<Surface> root = Tree<Surface>(value: surface);
  while (root.ancestors.isNotEmpty &&
      root.value.relation.dependency == SurfaceDependency.dependent) {
    root = root.ancestors.first;
  }
  return _spanningTree(null, root.value,
      (Surface s) => s.relation.dependency == SurfaceDependency.dependent);
}

/// Returns the List (forest) of DependentSpanningTrees in the current graph
List<Tree<Surface>> getDependentSpanningTrees(Surface surface) {
  List<Tree<Surface>> queue = <Tree<Surface>>[];
  Forest<Surface> forest = Forest<Surface>();

  Tree<Surface> tree = _spanningTree(null, surface, (Surface s) => true);

  queue.add(tree);
  while (queue.isNotEmpty) {
    Tree<Surface> t = queue.removeAt(0);
    List<Tree<Surface>> ends = _endsOfChain(current: t);
    queue.addAll(ends);
    for (Tree<Surface> s in ends) {
      t.find(s.value).detach();
    }
    forest.add(t);
  }
  return forest.roots.toList();
}

List<Tree<Surface>> _endsOfChain({Tree<Surface> current}) {
  List<Tree<Surface>> ends = <Tree<Surface>>[];
  for (Tree<Surface> s in current.children) {
    if (s.value.relation.dependency != SurfaceDependency.dependent) {
      ends.add(s);
    } else {
      ends.addAll(_endsOfChain(current: s));
    }
  }
  return ends;
}

/// Spans the full tree of all copresenting surfaces starting with this
Tree<Surface> getCopresentSpanningTree(Surface surface) {
  return _spanningTree(
      null,
      surface, // default to co-present if no opinion presented
      (Surface s) =>
          s.relation.arrangement == SurfaceArrangement.copresent ||
          s.relation.arrangement == SurfaceArrangement.none);
}

/// Gets the pattern spanning tree the current widget is part of
Tree<Surface> patternSpanningTree(
    SurfaceGraph graph, Tree<String> node, String pattern) {
  Tree<Surface> root = Tree<Surface>(value: graph.getNode(node.value));
  while (
      root.ancestors.isNotEmpty && root.value.compositionPattern == pattern) {
    root = root.ancestors.first;
  }
  return _spanningTree(
      null, root.value, (Surface s) => s.compositionPattern == pattern);
}

/// Gets the spanning tree of Surfaces participating in the Container
/// identified by containerId
Tree<Surface> getContainerSpanningTree(
    SurfaceGraph graph, Surface surface, String containerId) {
  Tree<String> containerNode = surface.node.root.find(containerId);
  // log.info('found: $node');
  Tree<Surface> root =
      Tree<Surface>(value: graph.getNode(containerNode.value));
  if (root.value is SurfaceContainer) {
    return _spanningTree(
      null,
      root.value,
      (Surface s) =>
          // TODO: (djmurphy) this will fail nested containers
          s.properties.containerMembership != null &&
          s.properties.containerMembership.contains(containerId),
    );
  } else {
    return root;
  }
}
