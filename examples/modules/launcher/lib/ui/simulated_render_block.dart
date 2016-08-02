// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:flutter/gestures.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import 'rk4_spring_simulation.dart';

const double _kExtentSimulationTension = 150.0;
const double _kExtentSimulationFriction = 25.0;
const RK4SpringDescription kExtentSimulationDesc = const RK4SpringDescription(
    tension: _kExtentSimulationTension, friction: _kExtentSimulationFriction);
const double _kMinimumExtentScale = 0.0;
const double _kFoldingSimulationTension = 150.0;
const double _kFoldingSimulationFriction = 25.0;
const RK4SpringDescription _kFoldingSimulationDesc = const RK4SpringDescription(
    tension: _kFoldingSimulationTension, friction: _kFoldingSimulationFriction);
const double _kOpacitySimulationTension = 100.0;
const double _kOpacitySimulationFriction = 25.0;
const RK4SpringDescription _kOpacitySimulationSpringDescription =
    const RK4SpringDescription(
        tension: _kOpacitySimulationTension,
        friction: _kOpacitySimulationFriction);

/// If true, the paint order of the objects in the [SimulatedRenderBlock] will
/// be reversed when the [SimulatedRenderBlock] is reversed.  This has the
/// effect of inverting which objects visually appear above others.
const bool _kReversedSwitchesPaintOrder = false;

abstract class SimulatedChildRenderObjectWidget implements RenderObjectWidget {}

abstract class TickingRenderBlock extends RenderBlock {
  Ticker _ticker;
  Duration _lastTick;

  TickingRenderBlock({List<RenderBox> children, Axis mainAxis: Axis.vertical})
      : super(children: children, mainAxis: mainAxis);

  @override
  void attach(PipelineOwner owner) {
    super.attach(owner);
    _startTicking();
  }

  @override
  void detach() {
    _ticker?.stop();
    _ticker = null;
    super.detach();
  }

  @override
  void markNeedsLayout() {
    super.markNeedsLayout();
    _startTicking();
  }

  /// Called once per tick with [elapsedSeconds] indicating how long it's
  /// been since the last tick.
  bool handleTick(double elapsedSeconds);

  void _startTicking() {
    if (_ticker?.isTicking ?? false) {
      return;
    }
    _ticker = new Ticker(_onTick);
    _lastTick = Duration.ZERO;
    _ticker.start();
  }

  void _onTick(Duration elapsed) {
    final double elapsedSeconds =
        (elapsed.inMicroseconds - _lastTick.inMicroseconds) / 1000000.0;
    _lastTick = elapsed;

    bool continueTicking = handleTick(elapsedSeconds);

    super.markNeedsLayout();

    if (!continueTicking) {
      _ticker?.stop();
      _ticker = null;
    }
  }
}

abstract class SimulatedRenderBlock extends TickingRenderBlock {
  RK4SpringSimulation extentSimulation;

  SimulatedRenderBlock(
      {Axis mainAxis,
      bool simulateSize: false,
      bool simulateOpacity: false,
      bool folded: false,
      bool reversed: false})
      : _simulateSize = simulateSize,
        _simulateOpacity = simulateOpacity,
        _folded = folded,
        _reversed = reversed,
        super(mainAxis: mainAxis) {
    this.mainAxis = mainAxis;
  }

  /// Expected to be implemented by children.  This positions the children
  /// within the [SimulatedRenderBlock].
  void positionChildren(BoxConstraints innerConstraints, double childExtentSum);

  /// True if the children's size should be simulated.
  bool get simulateSize => _simulateSize;
  bool _simulateSize;
  set simulateSize(bool value) {
    if (_simulateSize != value) {
      _simulateSize = value;
      markNeedsLayout();
    }
  }

  /// True if the children's alpha should be simulated.
  bool get simulateOpacity => _simulateOpacity;
  bool _simulateOpacity;
  set simulateOpacity(bool value) {
    if (_simulateOpacity != value) {
      _simulateOpacity = value;
      // A layout is necessary to start/stop the opacity simulation.
      markNeedsLayout();
    }
  }

  /// True if our target is folding.
  bool get folded => _folded;
  bool _folded;
  set folded(bool value) {
    if (_folded != value) {
      _folded = value;
      markNeedsLayout();
    }
  }

  /// True if our target is reversed.
  bool get reversed => _reversed;
  bool _reversed;
  set reversed(bool value) {
    if (_reversed != value) {
      _reversed = value;
      markNeedsLayout();
    }
  }

  bool get isVertical => mainAxis == Axis.vertical;

  @override
  void setupParentData(RenderBox child) {
    if (child.parentData is! SimulatedBlockParentData) {
      child.parentData = new SimulatedBlockParentData(
          isVertical: isVertical,
          simulateSize: simulateSize,
          simulateOpacity: simulateOpacity);
    }
  }

  @override
  bool hitTestChildren(HitTestResult result, {Point position}) {
    RenderBox child =
        reversed && _kReversedSwitchesPaintOrder ? lastChild : firstChild;
    while (child != null) {
      final SimulatedBlockParentData childParentData = child.parentData;
      Point transformed = new Point(position.x - childParentData.offset.dx,
          position.y - childParentData.offset.dy);
      if (child.hitTest(result, position: transformed)) {
        return true;
      }
      child = reversed && _kReversedSwitchesPaintOrder
          ? childParentData.previousSibling
          : childParentData.nextSibling;
    }
    return false;
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    context.pushClipRect(needsCompositing, offset, Offset.zero & size,
        (PaintingContext context, Offset offset) {
      RenderBox child =
          reversed && _kReversedSwitchesPaintOrder ? firstChild : lastChild;
      while (child != null) {
        final SimulatedBlockParentData childParentData = child.parentData;
        if (!_simulateSize ||
            (((childParentData.widthSimulation?.value ?? 0.0) > 0.0) &&
                ((childParentData.heightSimulation?.value ?? 0.0) > 0.0))) {
          if (!_simulateOpacity) {
            context.paintChild(child, childParentData.offset + offset);
          } else {
            double opacity = childParentData.opacitySimulation?.value ??
                (_simulateSize ? 0.0 : 1.0);
            int alpha = math.min(255, math.max(0, (opacity * 255.0).round()));
            switch (alpha) {
              case 0:
                // Don't draw when fully transparent.
                break;
              case 255:
                context.paintChild(child, childParentData.offset + offset);
                break;
              default:
                context.pushOpacity(
                    offset,
                    alpha,
                    (PaintingContext context, Offset offset) => context
                        .paintChild(child, childParentData.offset + offset));
                break;
            }
          }
        }
        child = reversed && _kReversedSwitchesPaintOrder
            ? childParentData.nextSibling
            : childParentData.previousSibling;
      }
    });
  }

  @override
  bool get alwaysNeedsCompositing => true;

  @override
  void performLayout() {
    // From super.performLayout():
    assert((isVertical
            ? constraints.maxHeight >= double.INFINITY
            : constraints.maxWidth >= double.INFINITY) &&
        'RenderBlock does not clip or resize its children, so it must be placed in a parent that does not constrain '
        'the block\'s main direction. You probably want to put the RenderBlock inside a RenderViewport.'
        is String);

    // Not from super.performLayout():
    BoxConstraints innerConstraints = _getInnerConstraints(constraints);
    double childExtentSum = _layoutChildren(innerConstraints);
    positionChildren(innerConstraints, childExtentSum);

    extentSimulation ??= new RK4SpringSimulation(
        initValue: childExtentSum, desc: kExtentSimulationDesc);
    extentSimulation.target = childExtentSum;

    // From super.performLayout():
    size = isVertical
        ? constraints
            .constrain(new Size(constraints.maxWidth, extentSimulation.value))
        : constraints
            .constrain(new Size(extentSimulation.value, constraints.maxHeight));
    assert(!size.isInfinite);
  }

  /// Returns the sum of the children's final height (if direction is vertical)
  /// or final width (if the direction is horizontal).
  double _layoutChildren(BoxConstraints innerConstraints) {
    double childExtentSum = 0.0;
    RenderBox child = firstChild;
    while (child != null) {
      final SimulatedBlockParentData childParentData = child.parentData;

      childExtentSum +=
          childParentData.layout(child, innerConstraints, folded: folded);

      // Verify everything's still good.
      assert(child.parentData == childParentData);

      child = childParentData.nextSibling;
    }
    return childExtentSum;
  }

  BoxConstraints _getInnerConstraints(BoxConstraints constraints) {
    if (isVertical)
      return new BoxConstraints.tightFor(
          width: constraints.constrainWidth(constraints.maxWidth));
    return new BoxConstraints.tightFor(
        height: constraints.constrainHeight(constraints.maxHeight));
  }

  @override
  bool handleTick(double elapsedSeconds) {
    bool continueTicking = false;

    if (extentSimulation != null) {
      extentSimulation.elapseTime(elapsedSeconds);
      if (!extentSimulation.isDone) {
        continueTicking = true;
      }
    }

    RenderBox child = firstChild;
    while (child != null) {
      final SimulatedBlockParentData childParentData = child.parentData;
      if (childParentData.onTick(elapsedSeconds)) {
        continueTicking = true;
      }
      child = childParentData.nextSibling;
    }

    return continueTicking;
  }
}

/// Holds parent data for each child of a [RenderBlock] that performs
/// simulations.
/// [beginSimulation] is the current simluation of the child's beginning (top or
/// left).
/// [widthSimulation] is the current simulation of the child's width.
/// [heightSimulation] is the current simulation of the child's height.
class SimulatedBlockParentData extends BlockParentData {
  final bool isVertical;
  final bool simulateSize;
  final bool simulateOpacity;
  final RK4SpringDescription beginSimulationDescription;
  SimulatedBlockParentData(
      {this.isVertical,
      this.simulateSize,
      this.simulateOpacity,
      this.beginSimulationDescription: _kFoldingSimulationDesc});

  RK4SpringSimulation beginSimulation;
  RK4SpringSimulation widthSimulation;
  RK4SpringSimulation heightSimulation;
  RK4SpringSimulation opacitySimulation;
  double overlap = 0.0;

  /// If [simulateSize] is true, [innerContraints] must be non-null.
  void position(RenderBox child, BoxConstraints innerConstraints,
      double initialChildBegin, double targetChildBegin,
      {bool folded: false}) {
    // If we haven't started a beginning simulation, start one!
    beginSimulation ??= new RK4SpringSimulation(
        initValue: initialChildBegin, desc: beginSimulationDescription);

    beginSimulation.target = folded ? initialChildBegin : targetChildBegin;

    // Position the child based on its simulated beginning and its current
    // simulated size, if applicable.
    final double childBegin = beginSimulation.value;

    final double heightSizeShift = _getHeightSizeShift(child, innerConstraints);
    final double widthSizeShift = _getWidthSizeShift(child, innerConstraints);
    offset = isVertical
        ? new Offset(widthSizeShift, childBegin + heightSizeShift)
        : new Offset(childBegin + widthSizeShift, heightSizeShift);
  }

  double _getHeightSizeShift(RenderBox child, BoxConstraints innerConstraints) {
    if (!simulateSize) {
      return 0.0;
    }
    final double maxHeight =
        child.getMaxIntrinsicHeight(innerConstraints.maxWidth);
    return (maxHeight - heightSimulation.value) / 2.0;
  }

  double _getWidthSizeShift(RenderBox child, BoxConstraints innerConstraints) {
    if (!simulateSize) {
      return 0.0;
    }
    final double maxWidth =
        child.getMaxIntrinsicWidth(innerConstraints.maxHeight);
    return (maxWidth - widthSimulation.value) / 2.0;
  }

  double layout(RenderBox child, BoxConstraints innerConstraints,
      {bool folded: false}) {
    final double maxHeight =
        child.getMaxIntrinsicHeight(innerConstraints.maxWidth);
    final double maxWidth =
        child.getMaxIntrinsicWidth(innerConstraints.maxHeight);
    final double childextent = isVertical ? maxHeight : maxWidth;

    BoxConstraints layoutConstraints;
    if (simulateSize) {
      // If we haven't started a height simulation, start one!
      heightSimulation ??= new RK4SpringSimulation(
          initValue: _kMinimumExtentScale * maxHeight,
          desc: kExtentSimulationDesc);

      heightSimulation.target =
          folded ? _kMinimumExtentScale * maxHeight : maxHeight;

      // If we haven't started a width simulation, start one!
      widthSimulation ??= new RK4SpringSimulation(
          initValue: _kMinimumExtentScale * maxWidth,
          desc: kExtentSimulationDesc);

      widthSimulation.target =
          folded ? _kMinimumExtentScale * maxWidth : maxWidth;

      // Constrain child to its simulated height/width.
      layoutConstraints = new BoxConstraints.tightFor(
          width: widthSimulation.value, height: heightSimulation.value);
    } else {
      layoutConstraints = innerConstraints;
    }

    if (simulateOpacity) {
      opacitySimulation ??= new RK4SpringSimulation(
          initValue: 0.0, desc: _kOpacitySimulationSpringDescription);

      // Update target opacity in case folded has changed.
      opacitySimulation.target = folded ? 0.0 : 1.0;
    }

    // Layout the child.
    child.layout(layoutConstraints, parentUsesSize: true);

    return childextent - overlap;
  }

  bool onTick(double elapsedSeconds) {
    bool continueTicking = false;
    // Update beginning simulation.
    if (beginSimulation != null) {
      beginSimulation.elapseTime(elapsedSeconds);
      if (!beginSimulation.isDone) {
        continueTicking = true;
      }
    }

    // Update height simulation.
    if (heightSimulation != null) {
      heightSimulation.elapseTime(elapsedSeconds);
      if (!heightSimulation.isDone) {
        continueTicking = true;
      }
    }

    // Update width simulation.
    if (widthSimulation != null) {
      widthSimulation.elapseTime(elapsedSeconds);
      if (!widthSimulation.isDone) {
        continueTicking = true;
      }
    }

    // Update opacity simulation.
    if (opacitySimulation != null) {
      opacitySimulation.elapseTime(elapsedSeconds);
      if (!opacitySimulation.isDone) {
        continueTicking = true;
      }
    }

    return continueTicking;
  }
}
