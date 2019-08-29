// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:meta/meta.dart';

/// Simple mutable tree data structure
class Tree<T> extends Iterable<Tree<T>> {
  /// Construct [Tree]
  Tree({@required this.value, Iterable<Tree<T>> children}) {
    children?.forEach(add);
  }

  /// The nodes value
  final T value;

  /// The longest path of edges to a leaf
  int get height {
    int h = 0;
    for (Tree<T> t in _children) {
      h = max(h, t.height + 1);
    }
    return h;
  }

  /// Direct descendents of this
  Iterable<Tree<T>> get children => _children.toList(growable: false);
  final List<Tree<T>> _children = <Tree<T>>[];

  /// Direct descendents of parent, except this
  Iterable<Tree<T>> get siblings => (_parent == null)
      ? Iterable<Tree<T>>.empty() // ignore: prefer_const_constructors
      : _parent.children.where((Tree<T> node) => node != this);

  /// Direct ancestors of this, starting at parent to root
  Iterable<Tree<T>> get ancestors {
    List<Tree<T>> ancestors = <Tree<T>>[];
    Tree<T> ancestor = this;
    while (ancestor._parent != null) {
      ancestor = ancestor._parent;
      ancestors.add(ancestor);
    }
    return ancestors;
  }

  /// Direct ancestor of this
  Tree<T> get parent => _parent;
  Tree<T> _parent;

  /// The root of the tree this node is a part of
  Tree<T> get root {
    Tree<T> node = this;
    while (node._parent != null) {
      node = node._parent;
    }
    return node;
  }

  @override
  Iterator<Tree<T>> get iterator {
    return flatten().iterator;
  }

  /// Breadth first flattening of tree
  Iterable<Tree<T>> flatten({
    int orderChildren(Tree<T> l, Tree<T> r),
  }) {
    List<Tree<T>> nodes = <Tree<T>>[this];
    for (int i = 0; i < nodes.length; i++) {
      Tree<T> node = nodes[i];
      if (orderChildren == null) {
        nodes.addAll(node._children);
      } else {
        nodes.addAll(node._children.toList()..sort(orderChildren));
      }
    }
    return nodes;
  }

  /// Detach this tree from its parents tree
  void detach() {
    if (parent != null) {
      _parent._children.remove(this);
      _parent = null;
    }
  }

  /// Add a child to this tree
  void add(Tree<T> child) {
    assert(child != null);
    _children.add(child);
    child._parent = this;
  }

  /// Find the single Tree node with the following value
  ///
  /// Note: Search order not specified (so make sure values are unique)
  Tree<T> find(T value) =>
      firstWhere((Tree<T> node) => node.value == value, orElse: () => null);

  /// Generate a new tree with the same structure with transformed values
  Tree<V> mapTree<V>(V f(T value)) => Tree<V>(
        value: f(value),
        children: _children.map((Tree<T> n) => n.mapTree(f)),
      );

  /// Reduces a tree to some other object using passed in function.
  V reduceTree<V>(V f(T value, Iterable<V> children)) =>
      f(value, children.map((Tree<T> child) => child.reduceTree(f)));

  /// Get a flattened iterable of all of the values in the tree
  Iterable<T> get values => flatten().map((Tree<T> t) => t.value);

  @override
  String toString() => 'Tree($values)';
}

/// A collection of trees
class Forest<T> extends Iterable<Tree<T>> {
  /// Construct [Forest]
  Forest({Iterable<Tree<T>> roots}) {
    roots?.forEach(add);
  }

  /// Root nodes of this forest
  Iterable<Tree<T>> get roots => _roots.toList(growable: false);
  final List<Tree<T>> _roots = <Tree<T>>[];

  /// The longest path of edges to a leaf
  int get height => _roots.isEmpty
      ? 0
      : _roots.fold(0, (int h, Tree<T> t) => max(h, t.height));

  /// Add a root node to this forest
  void add(Tree<T> node) {
    assert(node != null);
    node.detach();
    _roots.add(node);
  }

  /// Removes the node from the tree, and reparents children.
  ///
  /// Reparents its children to the nodes parent or as root nodes.
  void remove(Tree<T> node) {
    assert(node != null);
    if (contains(node)) {
      Tree<T> parent = node.parent;
      if (parent == null) {
        node.children.forEach(add);
        _roots.remove(node);
      } else {
        node.detach();
        node.children.forEach(parent.add);
      }
    }
  }

  @override
  Iterator<Tree<T>> get iterator {
    return flatten().iterator;
  }

  /// Breadth first flattening of tree
  Iterable<Tree<T>> flatten({
    int orderChildren(Tree<T> l, Tree<T> r),
  }) {
    List<Tree<T>> roots = _roots.toList();
    if (orderChildren != null) {
      roots.sort(orderChildren);
    }
    List<Tree<T>> nodes = <Tree<T>>[];
    for (Tree<T> node in roots) {
      nodes.addAll(node.flatten(orderChildren: orderChildren));
    }
    return nodes;
  }

  /// Find the single Tree node with the following value
  ///
  /// Note: Search order not specified (so make sure values are unique)
  Tree<T> find(T value) =>
      firstWhere((Tree<T> node) => node.value == value, orElse: () => null);

  /// Generate a new forest with the same structure with transformed values
  Forest<V> mapForest<V>(V f(T value)) => Forest<V>(
        roots: _roots.map((Tree<T> n) => n.mapTree(f)),
      );

  /// Reduces a Forest to a list of objects.
  Iterable<V> reduceForest<V>(V f(T value, Iterable<V> children)) =>
      roots.map((Tree<T> t) => t.reduceTree(f));

  /// Get a flattened iterable of all of the values in the forest
  Iterable<T> get values => flatten().map((Tree<T> t) => t.value);

  @override
  String toString() => 'Forest($roots)';
}
