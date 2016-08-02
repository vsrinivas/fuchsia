// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import 'simulated_render_block.dart';

const double _kFoldingOvershoot = 0.0;

/// A [SpringColumn] unfolds its [children] from its top or bottom.  These
/// children will also scale up during the unfold if [simulateSize] is true.
/// [SpringColumn] differs from [FoldingColumn] in that new elements added to
/// the column unfold from the top instead of the bottom.  In addition, the
/// entire [SpringColumn] cannot be folded up, unlike [FoldingColumn].
/// They will also fade in/out if [simulateOpacity] is true.
/// If [reversed] is true elements will extend from the bottom of the spring.
class SpringColumn extends StatelessWidget {
  final List<Widget> children;
  final bool simulateSize;
  final bool simulateOpacity;
  final bool reversed;
  SpringColumn(
      {Key key,
      this.children,
      this.simulateSize: false,
      this.simulateOpacity: true,
      this.reversed: false})
      : super(key: key);
  @override
  Widget build(_) => new SpringBlockBody(
      children: children,
      simulateSize: simulateSize,
      simulateOpacity: simulateOpacity,
      reversed: reversed);
}

/// Renders its [children] as described by [SpringColumn].
/// [mainAxis] indicates the direction of unfolding.
/// [simulateSize] will scale the children during folding/unfolding if true.
/// [simulateOpacity] will fade in/out the children during folding/unfolding if
/// true.
class SpringBlockBody extends MultiChildRenderObjectWidget
    implements SimulatedChildRenderObjectWidget {
  /// The direction to use as the main axis.
  final Axis mainAxis;
  final bool simulateSize;
  final bool simulateOpacity;
  final bool reversed;

  SpringBlockBody(
      {Key key,
      List<Widget> children: const <Widget>[],
      this.mainAxis: Axis.vertical,
      this.simulateSize: false,
      this.simulateOpacity: true,
      this.reversed: false})
      : super(key: key, children: children);

  @override
  SpringRenderBlock createRenderObject(_) => new SpringRenderBlock(
      mainAxis: mainAxis,
      simulateSize: simulateSize,
      simulateOpacity: simulateOpacity,
      reversed: reversed);

  @override
  void updateRenderObject(_, SpringRenderBlock renderObject) {
    renderObject.mainAxis = mainAxis;
    renderObject.simulateSize = simulateSize;
    renderObject.folded = false;
  }
}

/// Manages the rendering for [SpringBlockBody].
/// See [SpringBlockBody] for descriptions of [mainAxis], [simulateSize], and
/// [simulateOpacity].
class SpringRenderBlock extends SimulatedRenderBlock {
  SpringRenderBlock(
      {Axis mainAxis,
      bool simulateSize: false,
      bool simulateOpacity: true,
      bool reversed: false})
      : super(
            mainAxis: mainAxis,
            simulateSize: simulateSize,
            simulateOpacity: simulateOpacity,
            reversed: reversed,
            folded: false);

  @override
  void positionChildren(
      BoxConstraints innerConstraints, double childExtentSum) {
    if (reversed) {
      _positionChildrenReversed(innerConstraints, childExtentSum);
    } else {
      _positionChildrenForward(innerConstraints);
    }
  }

  void _positionChildrenForward(BoxConstraints innerConstraints) {
    double previousChildBegin = 0.0;
    double previousChildExtent = 0.0;

    RenderBox child = firstChild;
    while (child != null) {
      final SimulatedBlockParentData childParentData = child.parentData;

      final double childExtent = (isVertical
              ? child.getMaxIntrinsicHeight(innerConstraints.maxWidth)
              : child.getMaxIntrinsicWidth(innerConstraints.maxHeight)) -
          childParentData.overlap;

      childParentData.position(child, innerConstraints, previousChildBegin,
          previousChildBegin + previousChildExtent);

      previousChildBegin = childParentData.beginSimulation.value;
      previousChildExtent = childExtent;
      // Verify everything's still good.
      assert(child.parentData == childParentData);

      child = childParentData.nextSibling;
    }
  }

  void _positionChildrenReversed(
      BoxConstraints innerConstraints, double childExtentSum) {
    double previousChildBegin;

    RenderBox child = lastChild;
    while (child != null) {
      final SimulatedBlockParentData childParentData = child.parentData;

      final double childExtent = (mainAxis == Axis.vertical
              ? child.getMaxIntrinsicHeight(innerConstraints.maxWidth)
              : child.getMaxIntrinsicWidth(innerConstraints.maxHeight)) -
          childParentData.overlap;

      childParentData.position(
          child,
          innerConstraints,
          (child == lastChild)
              ? childExtentSum - childExtent
              : previousChildBegin,
          (child == lastChild)
              ? childExtentSum - childExtent
              : previousChildBegin - childExtent);

      previousChildBegin = childParentData.beginSimulation.value;

      // Verify everything's still good.
      assert(child.parentData == childParentData);

      child = childParentData.previousSibling;
    }
  }
}
