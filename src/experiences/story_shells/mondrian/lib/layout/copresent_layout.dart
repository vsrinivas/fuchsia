// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:flutter/widgets.dart';

import '../models/layout_model.dart';
import '../models/surface/positioned_surface.dart';
import '../models/surface/surface.dart';
import '../models/surface/surface_graph.dart';
import '../models/tree/spanning_tree.dart';
import '../models/tree/tree.dart';

// Convenience comparator used to ensure more focused items get higher priority
int _compareByOtherList(Surface l, Surface r, List<Surface> otherList) {
  int li = otherList.indexOf(l);
  int ri = otherList.indexOf(r);
  if (li < 0) {
    li = otherList.length;
  }
  if (ri < 0) {
    ri = otherList.length;
  }
  return ri - li;
}

/// Returns in the order they should be stacked
List<PositionedSurface> layoutSurfaces(
  BuildContext context,
  SurfaceGraph graph,
  List<Surface> focusStack,
  LayoutModel layoutModel,
) {
  Surface focused = focusStack.lastWhere((Surface surface) {
    return surface.connection != null;
  }, orElse: () => null);
  if (focusStack.isEmpty || focused == null) {
    return <PositionedSurface>[];
  }
  SurfaceArrangement focusedArrangement = focused.relation.arrangement;

  Tree<Surface> copresTree = getCopresentSpanningTree(focused);

  // Ontop only applies if the currently focused mod has a parent. If there's
  // only one sutface in the stack, fall through to the logic below.
  if (focusedArrangement == SurfaceArrangement.ontop && focusStack.length > 1) {
    // Determine the parent's position and then place the focused surface on top.
    List<PositionedSurface> surfaces = layoutSurfaces(context, graph,
        focusStack.sublist(0, focusStack.length - 1), layoutModel);
    PositionedSurface parentSurface =
        surfaces.firstWhere((PositionedSurface positioned) {
      return positioned.surface == focused.parent;
    });
    PositionedSurface ontopSurface = PositionedSurface(
      surface: focused,
      position: parentSurface.position,
    );
    surfaces.add(ontopSurface);
    return surfaces;
  }

  int focusOrder(Tree<Surface> l, Tree<Surface> r) =>
      _compareByOtherList(l.value, r.value, focusStack);

  // Remove dismissed surfaces and surfaces without views and collapse tree
  for (Tree<Surface> node in copresTree) {
    if (node.value.dismissed || node.value.connection == null) {
      node.children.forEach(node.parent.add);
      node.detach();
    }
  }

  // Prune less focused surfaces where their min constraints do not fit
  double totalMinWidth = 0.0;
  for (Tree<Surface> node
      in copresTree.flatten(orderChildren: focusOrder).skipWhile(
    (Tree<Surface> node) {
      double minWidth = node.value.minWidth(min: layoutModel.minScreenRatio);
      if (totalMinWidth + minWidth > 1.0) {
        return false;
      }
      totalMinWidth += minWidth;
      return true;
    },
  )) {
    node.detach();
  }

  // Prune less focused surfaces where emphasis values cannot be respected
  double totalEmphasis = 0.0;
  Surface top = focused;
  Surface tightestFit = focused;
  for (Tree<Surface> node
      in copresTree.flatten(orderChildren: focusOrder).skipWhile(
    (Tree<Surface> node) {
      Surface prevTop = top;
      double prevTotalEmphasis = totalEmphasis;

      // Update top
      if (top.ancestors.contains(node.value)) {
        top = node.value;
        totalEmphasis *= prevTop.absoluteEmphasis(top);
      }
      double emphasis = node.value.absoluteEmphasis(top);
      totalEmphasis += emphasis;

      // Calculate min width available
      double tightestFitEmphasis = tightestFit.absoluteEmphasis(top);
      double extraWidth = emphasis / totalEmphasis -
          node.value.minWidth(min: layoutModel.minScreenRatio);
      double tightestFitExtraWidth = tightestFitEmphasis / totalEmphasis -
          tightestFit.minWidth(min: layoutModel.minScreenRatio);

      // Break if smallest or this doesn't fit
      if (min(tightestFitExtraWidth, extraWidth) < 0.0) {
        // Restore previous values
        top = prevTop;
        totalEmphasis = prevTotalEmphasis;
        return false;
      }

      // Update tightest fit
      if (extraWidth < tightestFitExtraWidth) {
        tightestFit = node.value;
      }
      return true;
    },
  )) {
    node.detach();
  }

  List<Surface> surfacesToDisplay =
      copresTree.map((Tree<Surface> t) => t.value).toList(growable: false);

  Iterable<Surface> arrangement =
      top.flattened.where((Surface s) => surfacesToDisplay.contains(s));

  // Layout rects for arrangement
  final List<PositionedSurface> layout = <PositionedSurface>[];
  double fractionalWidthOffset = 0.0;
  for (Surface surface in arrangement) {
    double fractionalWidth = surface.absoluteEmphasis(top) / totalEmphasis;
    double fractionalHeight = 1.0;
    layout.add(
      PositionedSurface(
        surface: surface,
        position: Rect.fromLTWH(
          fractionalWidthOffset,
          0.0,
          fractionalWidth,
          fractionalHeight,
        ),
      ),
    );
    fractionalWidthOffset += fractionalWidth;
  }
  return layout;
}
