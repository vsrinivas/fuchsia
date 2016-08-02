// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import 'rk4_spring_simulation.dart';
import 'simulated_render_block.dart';

const double _kHeightSimulationTension = 150.0;
const double _kHeightSimulationFriction = 25.0;
const RK4SpringDescription kHeightSimulationDesc = const RK4SpringDescription(
    tension: _kHeightSimulationTension, friction: _kHeightSimulationFriction);

/// A [ResizingStack] stacks its children on top of each other and always
/// aims to the size of the last child.
class ResizingStack extends StatelessWidget {
  final List<Widget> children;

  ResizingStack({Key key, this.children}) : super(key: key);

  @override
  Widget build(_) => new ResizingStackRenderObjectWidget(children: children);
}

/// Renders its [children] as described by [ResizingStack].
class ResizingStackRenderObjectWidget extends MultiChildRenderObjectWidget {
  ResizingStackRenderObjectWidget({Key key, List<Widget> children})
      : super(key: key, children: children);

  @override
  ResizingStackRenderObject createRenderObject(_) =>
      new ResizingStackRenderObject();

  @override
  void updateRenderObject(_, ResizingStackRenderObject renderObject) {}
}

/// Manages the rendering for [ResizingStackRenderObjectWidget].
class ResizingStackRenderObject extends TickingRenderBlock {
  RK4SpringSimulation _heightSimulation;

  @override
  void setupParentData(RenderBox child) {
    if (child.parentData is! ResizingStackBlockParentData) {
      child.parentData = new ResizingStackBlockParentData();
    }
  }

  @override
  void performLayout() {
    BoxConstraints innerConstraints = _getInnerConstraints(constraints);

    final double targetHeight =
        lastChild?.getMaxIntrinsicHeight(innerConstraints.maxWidth) ?? 0.0;

    RenderBox child = firstChild;
    while (child != null) {
      child.layout(innerConstraints, parentUsesSize: true);
      final ResizingStackBlockParentData childParentData = child.parentData;
      childParentData.heightSimulation ??=
          new RK4SpringSimulation(initValue: 0.0, desc: kHeightSimulationDesc);
      childParentData.heightSimulation.target = targetHeight;
      childParentData.offset = Offset.zero;
      child = childParentData.nextSibling;
    }

    _heightSimulation ??=
        new RK4SpringSimulation(initValue: 0.0, desc: kHeightSimulationDesc);
    _heightSimulation.target = targetHeight;

    size = constraints
        .constrain(new Size(constraints.maxWidth, _heightSimulation.value));
    assert(!size.isInfinite);
  }

  BoxConstraints _getInnerConstraints(BoxConstraints constraints) {
    switch (mainAxis) {
      case Axis.vertical:
        return new BoxConstraints.tightFor(
            width: constraints.constrainWidth(constraints.maxWidth));
      case Axis.horizontal:
      default:
        return new BoxConstraints.tightFor(
            height: constraints.constrainHeight(constraints.maxHeight));
    }
  }

  @override
  bool handleTick(double elapsedSeconds) {
    bool continueTicking = false;

    if (_heightSimulation != null) {
      _heightSimulation.elapseTime(elapsedSeconds);
      if (!_heightSimulation.isDone) {
        continueTicking = true;
      }
    }

    RenderBox child = firstChild;
    while (child != null) {
      final ResizingStackBlockParentData childParentData = child.parentData;
      // Update beginning simulation.
      if (childParentData.heightSimulation != null) {
        childParentData.heightSimulation.elapseTime(elapsedSeconds);
        if (!childParentData.heightSimulation.isDone) {
          continueTicking = true;
        }
      }
      child = childParentData.nextSibling;
    }

    return continueTicking;
  }
}

/// Holds the [heightSimulation] for a [ResizingStackRenderObject] child.
class ResizingStackBlockParentData extends BlockParentData {
  RK4SpringSimulation heightSimulation;
}
