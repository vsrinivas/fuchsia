// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/physics.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:meta/meta.dart';

import '../anim/flux.dart';
import '../anim/sim.dart';
import '../models/surface/surface_form.dart';
import '../models/tree/tree.dart';
import '../widgets/gestures.dart';
import 'surface_frame.dart';

const SpringDescription _kSimSpringDescription = SpringDescription(
  mass: 1.0,
  stiffness: 220.0,
  damping: 29.0,
);

const double _kGestureWidth = 32.0;

/// Delays the first animation by this amount.  This gives new mods time to
/// load.
const _introAnimationDelay = Duration(milliseconds: 3000);

/// Stages determine how things move, and how they can be manipulated
class SurfaceStage extends StatelessWidget {
  /// Construct a SurfaceStage with these forms
  const SurfaceStage({@required this.forms});

  /// The forms inside this stage
  final Forest<SurfaceForm> forms;

  @override
  Widget build(BuildContext context) {
    // If there is only one surface, do not fight for horizontal gestures,
    // assume Session Shell will handle story dismissal.
    final useGestures = forms.flatten().length > 1;

    final children = <Widget>[]..addAll(
        forms
            .reduceForest(
              (SurfaceForm f, Iterable<_SurfaceInstance> children) =>
                  _SurfaceInstance(
                    form: f,
                    dependents: children.toList(),
                    useGestures: useGestures,
                  ),
            )
            .toList()
              ..sort(
                (_SurfaceInstance a, _SurfaceInstance b) =>
                    b.form.depth.compareTo(a.form.depth),
              ),
      );

    if (useGestures) {
      // We add ignoring unidirectional horizontal drag detectors on the
      // edges so the ones added by the surfaces along the edges have
      // something to fight in the gesture arena (otherwise they always win
      // and accept gestures in the wrong direction).  This prevents drags
      // toward the edges of the screen from moving or dismissing the
      // associated surfaces.
      children.addAll(<Widget>[
        Positioned(
          left: -_kGestureWidth,
          top: _kGestureWidth,
          bottom: _kGestureWidth,
          width: 2.0 * _kGestureWidth,
          child: _createIgnoringGestureDetector(Direction.left),
        ),
        Positioned(
          right: -_kGestureWidth,
          top: _kGestureWidth,
          bottom: _kGestureWidth,
          width: 2.0 * _kGestureWidth,
          child: _createIgnoringGestureDetector(Direction.right),
        ),
      ]);
    }
    return Stack(
      fit: StackFit.expand,
      children: children,
    );
  }

  /// This gesture detector fights in the arena and ignores the horizontal drags
  /// in the given [direction] if it wins.
  Widget _createIgnoringGestureDetector(Direction direction) {
    return UnidirectionalHorizontalGestureDetector(
      direction: direction,
      behavior: HitTestBehavior.translucent,
      onHorizontalDragStart: (DragStartDetails details) {},
      onHorizontalDragUpdate: (DragUpdateDetails details) {},
      onHorizontalDragEnd: (DragEndDetails details) {},
    );
  }
}

/// Instantiation of a Surface in SurfaceStage
class _SurfaceInstance extends StatefulWidget {
  /// SurfaceLayout
  _SurfaceInstance({
    @required this.form,
    this.isDebugMode = false,
    this.dependents = const <_SurfaceInstance>[],
    this.useGestures,
  }) : super(key: form.key);

  /// The form of this Surface
  final SurfaceForm form;

  /// Dependent surfaces
  final List<_SurfaceInstance> dependents;

  final bool isDebugMode;

  final bool useGestures;

  @override
  _SurfaceInstanceState createState() => _SurfaceInstanceState();
}

class DummyTickerProvider implements TickerProvider {
  @override
  Ticker createTicker(TickerCallback onTick) => Ticker((_) {});
}

class _SurfaceInstanceState extends State<_SurfaceInstance>
    with TickerProviderStateMixin {
  FluxAnimation<Rect> get animation => _animation;
  ManualAnimation<Rect> _animation;

  bool isDragging = false;
  double depth = 0.0;

  @override
  void initState() {
    super.initState();
    //TODO:(alangardner): figure out elevation layering

    /// Delay the first animation to give time for the mod to load.  We do this
    /// with a dummy ticker that doesn't tick for the delay period.
    _animation = _createAnimation(DummyTickerProvider());
    Timer(_introAnimationDelay, () {
      setState(() {
        _animation = _createAnimation(this);
      });
    });
  }

  ManualAnimation<Rect> _createAnimation(TickerProvider tickerProvider) {
    return ManualAnimation<Rect>(
      value: widget.form.initPosition,
      velocity: Rect.zero,
      builder: (Rect value, Rect velocity) => MovingTargetAnimation<Rect>(
              vsync: tickerProvider,
              simulate: _kFormSimulate,
              target: _target,
              value: value,
              velocity: velocity)
          .stickyAnimation,
    )..done();
  }

  @override
  void didUpdateWidget(_SurfaceInstance oldWidget) {
    super.didUpdateWidget(oldWidget);
    _animation
      ..update(value: _animation.value, velocity: _animation.velocity)
      ..done();
  }

  FluxAnimation<Rect> get _target {
    final SurfaceForm f = widget.form;
    final _SurfaceInstanceState parentSurfaceState = context
        ?.findAncestorStateOfType<_SurfaceInstanceState>();
    return parentSurfaceState == null
        ? ManualAnimation<Rect>(value: f.position, velocity: Rect.zero)
        : TransformedAnimation<Rect>(
            animation: parentSurfaceState.animation,
            valueTransform: (Rect r) => f.position.shift(
                r.center - parentSurfaceState.widget.form.position.center),
            velocityTransform: (Rect r) => r,
          );
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _animation,
      builder: (BuildContext context, Widget child) {
        Size parentSize = MediaQuery.of(context).size;
        final SurfaceForm form = widget.form;
        Offset fractionalOffset =
            (animation.value.center - animation.value.size.center(Offset.zero));
        double left = parentSize.width * fractionalOffset.dx;
        double top = parentSize.height * fractionalOffset.dy;
        double right = parentSize.width *
            (1.0 - (fractionalOffset.dx + animation.value.size.width));
        double bottom = parentSize.height *
            (1.0 - (fractionalOffset.dy + animation.value.size.height));

        double surfaceDepth = isDragging
            ? -2.0
            : lerpDouble(
                form.depth,
                depth,
                fractionalOffset.dy.abs(),
              );

        final stackChildren = <Widget>[
          Positioned(
            left: left,
            top: top,
            bottom: bottom,
            right: right,
            child: SurfaceFrame(
              child: form.parts.keys.first,
              depth: surfaceDepth,
              // HACK(alangardner): May need explicit interactable parameter
              interactable: form.dragFriction != kDragFrictionInfinite,
            ),
          )
        ];

        if (widget.useGestures) {
          stackChildren.addAll([
            Positioned(
              left: left - _kGestureWidth,
              top: top + _kGestureWidth,
              bottom: bottom + _kGestureWidth,
              width: 2.0 * _kGestureWidth,
              child: _createGestureDetector(
                parentSize,
                form,
                Direction.right,
              ),
            ),
            Positioned(
              right: right - _kGestureWidth,
              top: top + _kGestureWidth,
              bottom: bottom + _kGestureWidth,
              width: 2.0 * _kGestureWidth,
              child: _createGestureDetector(
                parentSize,
                form,
                Direction.left,
              ),
            ),
          ]);
        }
        return Stack(
          fit: StackFit.expand,
          children: stackChildren..addAll(widget.dependents),
        );
      },
    );
  }

  Widget _createGestureDetector(
    Size parentSize,
    SurfaceForm form,
    Direction direction,
  ) {
    return UnidirectionalHorizontalGestureDetector(
      direction: direction,
      behavior: HitTestBehavior.translucent,
      onHorizontalDragStart: (DragStartDetails details) {
        _animation.update(
          value: animation.value,
          velocity: Rect.zero,
        );
        form.onDragStarted();
        isDragging = true;
      },
      onHorizontalDragUpdate: (DragUpdateDetails details) {
        _animation.update(
          value: animation.value.shift(
            _toFractional(
              form.dragFriction(
                _toAbsolute(
                  animation.value.center - form.position.center,
                  parentSize,
                ),
                details.delta,
              ),
              parentSize,
            ),
          ),
          velocity: Rect.zero,
        );
      },
      onHorizontalDragEnd: (DragEndDetails details) {
        _animation
          ..update(
            value: animation.value,
            velocity: Rect.zero.shift(
              _toFractional(
                form.dragFriction(
                  _toAbsolute(
                    animation.value.center - form.position.center,
                    parentSize,
                  ),
                  details.velocity.pixelsPerSecond,
                ),
                parentSize,
              ),
            ),
          )
          ..done();
        form.onDragFinished(
          _toAbsolute(
            animation.value.center - form.position.center,
            parentSize,
          ),
          details.velocity,
        );
        isDragging = false;
        depth = -2.0;
      },
    );
  }

  Offset _toFractional(Offset absoluteOffset, Size size) {
    return Offset(
      absoluteOffset.dx / size.width,
      absoluteOffset.dy / size.height,
    );
  }

  Offset _toAbsolute(Offset fractionalOffset, Size size) {
    return Offset(
      fractionalOffset.dx * size.width,
      fractionalOffset.dy * size.height,
    );
  }
}

const double _kEpsilon = 1e-3;
const Tolerance _kTolerance = Tolerance(
  distance: _kEpsilon,
  time: _kEpsilon,
  velocity: _kEpsilon,
);

Sim<Rect> _kFormSimulate(Rect value, Rect target, Rect velocity) =>
    IndependentRectSim(
      positionSim: Independent2DSim(
        xSim: SpringSimulation(
          _kSimSpringDescription,
          value.center.dx,
          target.center.dx,
          velocity.center.dx,
          tolerance: _kTolerance,
        ),
        ySim: SpringSimulation(
          _kSimSpringDescription,
          value.center.dy,
          target.center.dy,
          velocity.center.dy,
          tolerance: _kTolerance,
        ),
      ),
      sizeSim: Independent2DSim(
        xSim: SpringSimulation(
          _kSimSpringDescription,
          value.size.width,
          target.size.width,
          velocity.size.width,
          tolerance: _kTolerance,
        ),
        ySim: SpringSimulation(
          _kSimSpringDescription,
          value.size.height,
          target.size.height,
          velocity.size.height,
          tolerance: _kTolerance,
        ),
      ),
    );
