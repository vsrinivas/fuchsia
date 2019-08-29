// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import '../../models/surface/surface_transition.dart';
import '../tree/tree.dart';
import 'surface_graph.dart';
import 'surface_properties.dart';
import 'surface_relation_util.dart';

/// The parentId that means no parent
const String kNoParent = '';

/// Details of a surface child view
class Surface extends Model {
  /// Public constructor
  Surface(this._graph, this.node, this.properties, this.relation,
      this.compositionPattern, this.placeholderColor) {
    transitionModel = SurfaceTransitionModel()

      // notify listeners of Surface model to changes that happen in
      // surface_transition, so we can use the same model in builders
      /// ignore: unnecessary_lambdas
      ..addListener(() => notifyListeners());
  }

  Surface.fromJson(Map<String, dynamic> json, this._graph)
      : node = Tree<String>(value: json['id']),
        compositionPattern = json['compositionPattern'],
        properties = SurfaceProperties.fromJson(
            json['surfaceProperties'].cast<String, dynamic>()),
        relation = SurfaceRelationUtil.decode(
            json['surfaceRelation'].cast<String, String>()),
        childIds = json['children'].cast<String>(),
        isParentRoot = json['parentId'] == null,
        placeholderColor = json['placeholderColor'];

  final SurfaceGraph _graph;
  final Tree<String> node;

  /// The transitionModel handling placeholder timinggit
  SurfaceTransitionModel transitionModel;

  /// Connection to underlying view
  ChildViewConnection connection;

  /// The properties of this surface
  final SurfaceProperties properties;

  /// The relationship this node has with its parent
  final SurfaceRelation relation;

  /// The pattern with which to compose this node with its parent
  final String compositionPattern;

  /// The placeholder color to use if the surface is focused before the
  /// module is ready to display. This comes in as a hex string on the
  /// module manifest.
  final String placeholderColor;

  // Used to track whether this node is attached to the root of the graph
  bool isParentRoot = false;

  // Used for constructing the surface and its associated graph from json.
  // Note: these ids will not stay up to date with what's in the node.
  // Use the _children method below after the graph has been updated.
  List<String> childIds;

  /// Whether or not this surface is currently dismissed
  bool get dismissed => _graph.isDismissed(node.value);

  /// Return the min width of this Surface
  double minWidth({double min = 0.0}) => math.max(0.0, min);

  /// Return the absolute emphasis given some root displayed Surface
  double absoluteEmphasis(Surface relative) {
    assert(root == relative.root);
    Iterable<Surface> relativeAncestors = relative.ancestors;
    Surface ancestor = this;
    double emphasis = 1.0;
    while (ancestor != relative && !relativeAncestors.contains(ancestor)) {
      emphasis *= ancestor.relation.emphasis;
      ancestor = ancestor.parent;
    }
    Surface aRelative = relative;
    while (ancestor != aRelative) {
      emphasis /= aRelative.relation.emphasis;
      aRelative = aRelative.parent;
    }
    return emphasis;
  }

  /// The parent of this node
  Surface get parent => _surface(node.parent);

  /// The parentId of this node
  String get parentId => node.parent.value;

  /// The root surface
  Surface get root {
    List<Tree<String>> nodeAncestors = node.ancestors;
    return _surface(nodeAncestors.length > 1
        ? nodeAncestors[nodeAncestors.length - 2]
        : node);
  }

  /// The children of this node
  Iterable<Surface> get children => _surfaces(node.children);

  /// The siblings of this node
  Iterable<Surface> get siblings => _surfaces(node.siblings);

  /// The ancestors of this node
  Iterable<Surface> get ancestors => _surfaces(node.ancestors);

  /// This node and its descendents flattened into an Iterable
  Iterable<Surface> get flattened => _surfaces(node);

  /// Returns a Tree for this surface
  Tree<Surface> get tree {
    Tree<Surface> t = Tree<Surface>(value: this);
    for (Surface child in children) {
      t.add(child.tree);
    }
    return t;
  }

  /// Dismiss this node hiding it from layouts
  bool dismiss() => _graph.dismissSurface(node.value);

  /// Returns true if this surface can be dismissed
  bool canDismiss() => _graph.canDismissSurface(node.value);

  /// Remove this node from graph
  /// Returns true if this was removed
  bool remove() {
    // Only allow non-root surfaces to be removed
    if (node.parent?.value != null) {
      _graph.removeSurface(node.value);
      return true;
    }
    return false;
  }

  // Get the surface for this node
  Surface _surface(Tree<String> node) =>
      (node == null || node.value == null) ? null : _graph.getNode(node.value);

  Iterable<Surface> _surfaces(Iterable<Tree<String>> nodes) => nodes
      .where((Tree<String> node) => node != null && node.value != null)
      .map(_surface);

  @override
  String toString() {
    String edgeLabel = relation?.arrangement?.toString() ?? '';
    String edgeArrow = '$edgeLabel->'.padLeft(6, '-');
    String disconnected = connection == null ? '[DISCONNECTED]' : '';
    return '${edgeArrow}Surface ${node.value} $disconnected';
  }

  List<String> _children() {
    List<String> ids = [];
    for (Tree<String> child in node.children) {
      ids.add(child.value);
    }
    return ids;
  }

  Map<String, dynamic> toJson() {
    return {
      'id': node.value,
      'parentId': parentId,
      'surfaceRelation': SurfaceRelationUtil.toMap(relation),
      'surfaceProperties': properties,
      'compositionPattern': compositionPattern,
      'isDismissed': dismissed ? 'true' : 'false',
      'children': _children(),
      'placeholderColor': placeholderColor,
    };
  }
}

/// Defines a Container root in the [Surface Graph], holds the layout description
class SurfaceContainer extends Surface {
  SurfaceContainer(
      SurfaceGraph graph,
      Tree<String> node,
      SurfaceProperties properties,
      SurfaceRelation relation,
      String compositionPattern,
      this._layouts)
      : super(graph, node, properties, relation, compositionPattern, '') {
    super.connection = null;
  }

  @override
  set connection(ChildViewConnection value) {
    log.warning('Cannot set a child view connection on a Container');
  }

  /// returns the layouts for this container;
  List<ContainerLayout> get layouts => _layouts;

  final List<ContainerLayout> _layouts;
}
