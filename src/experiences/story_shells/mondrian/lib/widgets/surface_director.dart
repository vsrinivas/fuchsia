// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.widgets/model.dart';

import '../layout/container_layout.dart' as container;
import '../layout/copresent_layout.dart' as copresent;
import '../layout/pattern_layout.dart' as pattern;
import '../models/depth_model.dart';
import '../models/inset_manager.dart';
import '../models/layout_model.dart';
import '../models/surface/positioned_surface.dart';
import '../models/surface/surface.dart';
import '../models/surface/surface_form.dart';
import '../models/surface/surface_graph.dart';
import '../models/tree/spanning_tree.dart';
import '../models/tree/tree.dart';
import 'mondrian_child_view.dart';

import 'surface_stage.dart';

/// Directs the layout of the SurfaceSpace
class SurfaceDirector extends StatefulWidget {
  @override
  _SurfaceDirectorState createState() => _SurfaceDirectorState();
}

class _SurfaceDirectorState extends State<SurfaceDirector> {
  final Map<Surface, SurfaceForm> _prevForms = <Surface, SurfaceForm>{};
  final List<SurfaceForm> _orphanedForms = <SurfaceForm>[];

  SurfaceForm _form(
    PositionedSurface ps,
    double depth,
    FractionalOffset offscreen,
  ) {
    return SurfaceForm.single(
      key: GlobalObjectKey(ps.surface.node),
      child: MondrianChildView(
        surface: ps.surface,
        interactable: depth <= 0.0,
      ),
      position: ps.position,
      initPosition: ps.position.shift(Offset(offscreen.dx, offscreen.dy)),
      depth: depth,
      friction: depth > 0.0
          ? kDragFrictionInfinite
          : ps.surface.canDismiss()
              ? (Offset offset, Offset delta) =>
                  Offset(delta.dx * 0.6, delta.dy * 0.2)
              : (Offset offset, Offset delta) =>
                  delta / math.max(1.0, offset.distanceSquared / 100.0),
      onDragStarted: () {},
      onDragFinished: (Offset offset, Velocity velocity) {
        Offset expectedOffset = offset + (velocity.pixelsPerSecond / 5.0);
        // Only remove if greater than threshold
        if (expectedOffset.distance > 200.0) {
          // HACK(alangardner): Hardcoded distances for swipe gesture to
          // avoid complicated layout work.
          ps.surface.dismiss();
        }
      },
    );
  }

  SurfaceForm _orphanedForm(
      Surface surface, SurfaceForm form, FractionalOffset offscreen) {
    return SurfaceForm.single(
      key: form.key,
      child: MondrianChildView(
        surface: surface,
        interactable: false,
      ),
      position: form.position.shift(Offset(offscreen.dx, offscreen.dy)),
      initPosition: form.initPosition,
      depth: form.depth,
      friction: kDragFrictionInfinite,
      onPositioned: () {
        // TODO(alangardner): Callback to notify framework
        setState(() {
          _orphanedForms.removeWhere((SurfaceForm f) => f.key == form.key);
        });
      },
    );
  }

  @override
  Widget build(BuildContext context) => ScopedModelDescendant<InsetManager>(
        builder: (
          BuildContext context,
          Widget child,
          InsetManager insetManager,
        ) =>
            ScopedModelDescendant<LayoutModel>(
              builder: (
                BuildContext context,
                Widget child,
                LayoutModel layoutModel,
              ) =>
                  ScopedModelDescendant<SurfaceGraph>(
                    builder: (
                      BuildContext context,
                      Widget child,
                      SurfaceGraph graph,
                    ) =>
                        _buildStage(
                          context,
                          FractionalOffset.topRight,
                          insetManager,
                          layoutModel,
                          graph,
                        ),
                  ),
            ),
      );

  Widget _buildStage(
    BuildContext context,
    FractionalOffset offscreen,
    InsetManager insetManager,
    LayoutModel layoutModel,
    SurfaceGraph graph,
  ) {
    Map<Surface, SurfaceForm> placedSurfaces = <Surface, SurfaceForm>{};
    List<Surface> focusStack = graph.focusStack.toList();
    double depth = 0.0;
    while (focusStack.isNotEmpty &&
        focusStack.any((surface) {
          return surface.connection != null;
        })) {
      List<PositionedSurface> positionedSurfaces = <PositionedSurface>[];
      if (focusStack.isNotEmpty) {
        Surface last = focusStack.last;
        // purposefully giving compositionPattern top billing
        // here to avoid any codelab surprises but we will have
        // to harmonize this logic in future
        // TODO: (djmurphy, jphsiao)
        if (last.compositionPattern != null &&
            last.compositionPattern.isNotEmpty) {
          positionedSurfaces = pattern.layoutSurfaces(
            context,
            focusStack,
            layoutModel,
          );
        } else if (last.properties.containerMembership != null &&
            last.properties.containerMembership.isNotEmpty) {
          positionedSurfaces = container.layoutSurfaces(
            context,
            graph,
            last,
            layoutModel,
          );
        } else {
          positionedSurfaces = copresent.layoutSurfaces(
            context,
            graph,
            focusStack,
            layoutModel,
          );
        }
      }
      List<String> placedViewIds =
          placedSurfaces.keys.map((Surface s) => s.node.value).toList();
      for (PositionedSurface ps in positionedSurfaces) {
        // TODO (jphsiao): Make this check more thorough. A new surface may
        // have the same view id, but not the same arrangement, parent or
        // connection. These may result in animations for surfaces that are
        // already on screen that we do not handle yet.
        if (!placedViewIds.contains(ps.surface.node.value)) {
          _prevForms.removeWhere((Surface surface, SurfaceForm form) =>
              surface.node.value == ps.surface.node.value);
          // if there is already a Surface laid out, then the incoming
          // surface should default to 'summon'. If there is no Surface being
          // displayed, then do not animate.
          FractionalOffset surfaceOrigin = _prevForms.isNotEmpty
              ? offscreen
              : FractionalOffset(0.0, 0.0); //apply no initial translation
          if (ps.surface.relation.arrangement == SurfaceArrangement.ontop) {
            // Surfaces that are ontop will be placed above the current depth
            // TODO(jphsiao): Revisit whether ontop should be placed on top of
            // all surfaces or if it should push its parent back in z.
            placedSurfaces[ps.surface] = _form(ps, -1.0, surfaceOrigin);
          } else {
            placedSurfaces[ps.surface] = _form(ps, depth, surfaceOrigin);
          }
        }
      }
      depth = (depth + 0.1).clamp(0.0, 1.0);
      while (focusStack.isNotEmpty &&
          placedSurfaces.keys.contains(focusStack.last)) {
        focusStack.removeLast();
      }
    }
    Forest<Surface> dependentSpanningTrees = Forest<Surface>();
    if (placedSurfaces.isNotEmpty) {
      // Get the dependent spanning trees for each tree off of the root
      for (Tree<String> childTree in graph.root.children) {
        getDependentSpanningTrees(graph.getNode(childTree.value))
            .forEach(dependentSpanningTrees.add);
      }

      /// prune non-visible surfaces
      for (Tree<Surface> t in dependentSpanningTrees.flatten()) {
        if (!placedSurfaces.keys.contains(t.value)) {
          dependentSpanningTrees.remove(t);
        }
      }
    }

    // Convert orphaned forms, to animate them out
    Iterable<Key> placedKeys =
        placedSurfaces.values.map((SurfaceForm f) => f.key);
    _orphanedForms.removeWhere((SurfaceForm f) => placedKeys.contains(f.key));
    for (Surface s in _prevForms.keys) {
      _orphanedForms.add(_orphanedForm(s, _prevForms[s], offscreen));
    }
    _prevForms
      ..clear()
      ..addAll(placedSurfaces);

    /// Create form forest
    final Forest<SurfaceForm> formForest =
        dependentSpanningTrees.mapForest((Surface s) => placedSurfaces[s]);

    for (SurfaceForm orphan in _orphanedForms) {
      formForest.add(Tree<SurfaceForm>(value: orphan));
    }

    /// Determine the max and min depths of all visible surfaces.
    double minDepth = double.infinity;
    double maxDepth = -double.infinity;
    for (double depth
        in placedSurfaces.values.map((SurfaceForm f) => f.depth)) {
      if (minDepth > depth) {
        minDepth = depth;
      }
      if (maxDepth < depth) {
        maxDepth = depth;
      }
    }
    for (double depth in _orphanedForms.map((SurfaceForm f) => f.depth)) {
      if (minDepth > depth) {
        minDepth = depth;
      }
      if (maxDepth < depth) {
        maxDepth = depth;
      }
    }

    return ScopedModel<DepthModel>(
      model: DepthModel(minDepth: minDepth, maxDepth: maxDepth),
      child: SurfaceStage(forms: formForest),
    );
  }
}
