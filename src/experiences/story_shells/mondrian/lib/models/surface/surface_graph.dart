// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:developer' show Timeline;

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import '../tree/spanning_tree.dart';
import '../tree/tree.dart';
import 'surface.dart';
import 'surface_properties.dart';

// Data structure to manage the relationships and relative focus of surfaces
class SurfaceGraph extends Model {
  SurfaceGraph() {
    setupLogger(name: 'Mondrian');
  }

  SurfaceGraph.fromJson(Map<String, dynamic> json) {
    reload(json);
  }

  /// Cache of surfaces
  final Map<String, Surface> _surfaces = <String, Surface>{};

  /// Surface relationship tree
  final Tree<String> _tree = Tree<String>(value: null);

  /// The stack of previous focusedSurfaces, most focused at end
  final List<String> _focusedSurfaces = <String>[];

  /// The stack of previous focusedSurfaces, most focused at end
  final Set<String> _dismissedSurfaces = <String>{};

  /// A mapping between surfaces that were brought in as ModuleSource::External
  /// surfaces (e.g. suggestions) and surfaces that were visually present at
  /// their introduction, in order to track where to provide a shell affordance
  /// for resummoning external surfaces that have been dismissed
  /// (surfaces are identified by ID)
  final Map<String, String> _visualAssociation = <String, String>{};

  Tree<String> get root => _tree;

  /// The node corresponding to the given id.
  Surface getNode(String id) => _surfaces[id];

  /// The last focused surface.
  Surface _lastFocusedSurface;

  /// The currently most focused [Surface]
  Surface get focused =>
      _focusedSurfaces.isEmpty ? null : _surfaces[_focusedSurfaces.last];

  /// The history of focused [Surface]s
  Iterable<Surface> get focusStack => _focusedSurfaces
      .where(_surfaces.containsKey)
      .map((String id) => _surfaces[id]);

  /// Add a [Surface] to the graph with the given parameters.
  ///
  /// Returns the surface that was added to the graph.
  Surface addSurface(
    String id,
    SurfaceProperties properties,
    String parentId,
    SurfaceRelation relation,
    String pattern,
    String placeholderColor,
  ) {
    Tree<String> node = _tree.find(id) ?? Tree<String>(value: id);
    Tree<String> parent =
        (parentId == kNoParent) ? _tree : _tree.find(parentId);
    assert(parent != null);
    assert(relation != null);
    Surface oldSurface = _surfaces[id];
    Surface updatedSurface =
        Surface(this, node, properties, relation, pattern, placeholderColor);
    // if this is an external surface, create an association between this and
    // the most focused surface.
    if (properties.source == ModuleSource.external &&
        _focusedSurfaces.isNotEmpty) {
      _visualAssociation[_focusedSurfaces.last] = id;
    }
    if (oldSurface != null) {
      // TODO (jphsiao): this is a hack to handle the adding of a surface with
      // the same view id. In this case we assume the view is going to be
      // reused.
      updatedSurface.connection = oldSurface.connection;
    }
    _surfaces[id] = updatedSurface;
    // Do not add the child again if the parent already knows about it.
    if (!parent.children.contains(node)) {
      parent.add(node);
    }

    notifyListeners();
    return updatedSurface;
  }

  /// Removes [Surface] from graph
  void removeSurface(String id) {
    if (_surfaces.keys.contains(id)) {
      Tree<String> node = _tree.find(id);
      if (node != null) {
        node.detach();
        // Remove orphaned children
        for (Tree<String> child in node.children) {
          child.detach();
          // As a temporary policy, remove child surfaces when surfaces are
          // removed. This policy will be revisited when we have a better sense
          // of what to do with orphaned children.
          _surfaces[child.value].remove();
          _focusedSurfaces.remove(child.value);
          _dismissedSurfaces.remove(child.value);
        }
        _focusedSurfaces.remove(id);
        _dismissedSurfaces.remove(id);
        _surfaces.remove(id);
        notifyListeners();
      }
    }
  }

  /// Move the surface up in the focus stack, undismissing it if needed.
  void focusSurface(String id) {
    if (!_surfaces.containsKey(id)) {
      log.warning('Invalid surface id "$id"');
      return;
    }
    _dismissedSurfaces.remove(id);
    _focusedSurfaces
      ..remove(id)
      ..add(id);

    // Also request the input focus through the child view connection.
    ChildViewConnection connection = _surfaces[id].connection;
    if (connection != null) {
      _surfaces[id].connection.requestFocus();
      _lastFocusedSurface = _surfaces[id];
      notifyListeners();
    }
  }

  /// Add a container root to the surface graph
  void addContainer(
    String id,
    SurfaceProperties properties,
    String parentId,
    SurfaceRelation relation,
    List<ContainerLayout> layouts,
  ) {
    // TODO (djurphy): collisions/pathing - partial fix if we
    // make the changes so container IDs are paths.
    log.info('addContainer: $id');
    Tree<String> node = _tree.find(id) ?? Tree<String>(value: id);
    log.info('found or made node: $node');
    Tree<String> parent =
        (parentId == kNoParent) ? _tree : _tree.find(parentId);
    assert(parent != null);
    assert(relation != null);
    parent.add(node);
    Surface oldSurface = _surfaces[id];
    _surfaces[id] = SurfaceContainer(
        this, node, properties, relation, '' /*pattern*/, layouts);
    oldSurface?.notifyListeners();
    log.info('_surfaces[id]: ${_surfaces[id]}');
    notifyListeners();
  }

  /// Returns the list of surfaces that would be dismissed if this surface
  /// were dismissed - e.g. as a result of dependency - including this surface
  List<String> dismissedSet(String id) {
    Surface dismissed = _surfaces[id];
    List<Surface> ancestors = dismissed.ancestors.toList();
    List<Surface> dependentTree = getDependentSpanningTree(dismissed)
        .map((Tree<Surface> t) => t.value)
        .toList()
          // TODO(djmurphy) - when codependent comes in this needs to change
          // this only removes down the tree, codependents would remove their
          // ancestors
          ..removeWhere((Surface s) => ancestors.contains(s));
    List<String> depIds =
        dependentTree.map((Surface s) => s.node.value).toList();
    return depIds;
  }

  /// Check if given surface can be dismissed
  bool canDismissSurface(String id) {
    List<String> wouldDismiss = dismissedSet(id);
    return _focusedSurfaces
        .where((String fid) => !wouldDismiss.contains(fid))
        .isNotEmpty;
  }

  /// When called surface is no longer displayed
  bool dismissSurface(String id) {
    if (!canDismissSurface(id)) {
      return false;
    }
    List<String> depIds = dismissedSet(id);
    _focusedSurfaces.removeWhere((String fid) => depIds.contains(fid));
    _dismissedSurfaces.addAll(depIds);
    notifyListeners();
    return true;
  }

  /// True if surface has been dismissed and not subsequently focused
  bool isDismissed(String id) => _dismissedSurfaces.contains(id);

  /// Used to update a [Surface] with a live ChildViewConnection
  void connectView(String id, ViewHolderToken viewHolderToken) {
    final Surface surface = _surfaces[id];
    if (surface != null) {
      if (surface.connection != null) {
        // TODO(jphsiao): remove this hack once story shell API has been
        // change to accomodate view reusage
        return;
      }
      log.fine('connectView $surface');
      surface
        ..connection = ChildViewConnection(
          viewHolderToken,
          onAvailable: (ChildViewConnection connection) {
            Timeline.instantSync('surface available', arguments: {'id': '$id'});

            // If this surface is the last focused one, also request input focus
            if (_lastFocusedSurface == surface) {
              connection.requestFocus();
            }
            surface.notifyListeners();
          },
          onUnavailable: (ChildViewConnection connection) {
            Timeline.instantSync('surface $id unavailable');
            surface.connection = null;
            if (_surfaces.containsValue(surface)) {
              removeSurface(id);
              notifyListeners();
            }
            // Also any existing listener
            surface.notifyListeners();
          },
        )
        ..notifyListeners();
    }
  }

  void reload(Map<String, dynamic> json) {
    List<dynamic> decodedSurfaceList = json['surfaceList'];
    for (dynamic s in decodedSurfaceList) {
      Map<String, dynamic> item = s.cast<String, dynamic>();
      Surface surface = Surface.fromJson(item, this);

      _surfaces.putIfAbsent(surface.node.value, () {
        return surface;
      });
    }
    _surfaces.forEach((String id, Surface surface) {
      Tree<String> node = surface.node;
      if (surface.isParentRoot) {
        _tree.add(node);
      }
      if (surface.childIds != null) {
        for (String id in surface.childIds) {
          node.add(_surfaces[id].node);
        }
      }
    });
    dynamic list = json['focusStack'];
    List<String> focusStack = list.cast<String>();
    _focusedSurfaces.addAll(focusStack);
  }

  // Get the SurfaceIds of associated external surfaces
  // (surfaces originating from outside the current story)
  // that are dismissed and associated with the current Surface
  Set<String> externalSurfaces({String surfaceId}) {
    // Case1: An external child has a relationship with this surface
    // and the child has been dismissed
    Surface parent = getNode(surfaceId);
    List<Surface> externalSurfaces = parent.children.toList()
      ..retainWhere(
          (Surface s) => s.properties.source == ModuleSource.external);
    Set<String> externalIds =
        externalSurfaces.map((Surface s) => s.node.value).toSet();
    // Case2: The focused surface has a recorded visual association with an
    // external surface
    if (_visualAssociation[surfaceId].isNotEmpty) {
      externalIds.add(_visualAssociation[surfaceId]);
    }
    return externalIds;
  }

  /// Returns the amount of [Surface]s in the graph
  int get size => _surfaces.length;

  /// The tree size includes the root node which has no surface
  int get treeSize => _tree.flatten().length;

  @override
  String toString() =>
      'Tree:\n${_tree.children.map(_toString).join('\n')}\nfocusStack length ${focusStack.length}';

  String _toString(Tree<String> node, {String prefix = ''}) {
    String nodeString = '$prefix${_surfaces[node.value]}';
    if (node.children.isNotEmpty) {
      nodeString =
          '$nodeString\n${node.children.map((Tree<String> node) => _toString(node, prefix: '$prefix  ')).join('\n')}';
    }
    return '$nodeString';
  }

  Map<String, dynamic> toJson() {
    return {
      'surfaceList': _surfaces.values.toList(),
      'focusStack': _focusedSurfaces,
      'links': [], // TODO(jphsiao): plumb through link data
    };
  }
}
