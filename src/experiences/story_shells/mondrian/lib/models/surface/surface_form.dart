// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:meta/meta.dart';

/// A Form is a description of a thing in a Space
class SurfaceForm {
  /// Simple, single child SurfaceForm
  SurfaceForm.single({
    @required Key key,
    @required Widget child,
    @required Rect position,
    @required Rect initPosition,
    double depth,
    DragFriction friction,
    VoidCallback onPositioned,
    VoidCallback onDragStarted,
    DragCallback onDrag,
    DragCallback onDragFinished,
  }) : this.withParts(
            key: key,
            parts: <Widget, Rect>{child: Offset.zero & position.size},
            position: position,
            initPosition: initPosition,
            depth: depth,
            friction: friction,
            onPositioned: onPositioned,
            onDragStarted: onDragStarted,
            onDrag: onDrag,
            onDragFinished: onDragFinished);

  /// Constructed out of multiple rectancular parts
  SurfaceForm.withParts({
    @required this.key,
    @required this.parts,
    @required this.position,
    @required this.initPosition,
    this.depth = 0.0,
    DragFriction friction,
    VoidCallback onPositioned,
    VoidCallback onDragStarted,
    DragCallback onDrag,
    DragCallback onDragFinished,
  })  : dragFriction = friction ?? kDragFrictionNone,
        onPositioned = onPositioned ?? (() {}),
        onDragStarted = onDragStarted ?? (() {}),
        onDrag = onDrag ?? kDragCallbackNone,
        onDragFinished = onDragFinished ?? kDragCallbackNone;

  /// The Key to use when instantiating widgets from this form
  final Key key;

  /// Map of widgets to their rect shape, relative to position.offset.
  /// Determines the shape of the form.
  final Map<Widget, Rect> parts;

  /// The position of this form in the space.
  final Rect position;

  /// The summon position of this form in the space.
  final Rect initPosition;

  /// The z depth of this form in the space.
  final double depth;

  /// Friction for manipulation.
  final DragFriction dragFriction;

  /// Callbacks once desired position is reached
  final VoidCallback onPositioned;

  /// Called on drag events.
  final VoidCallback onDragStarted;

  /// Called on drag events.
  final DragCallback onDrag;

  /// Called when surface no longer being dragged
  final DragCallback onDragFinished;

  @override
  String toString() =>
      'SurfaceForm($key) with initPosition:$initPosition position:$position depth:$depth';
}

/// Generates a movement delta from a manipulation delta.
/// offset: the form offset vector from the target position
/// delta: the incremental manipulation vector
/// returns the form incremental offset
typedef DragFriction = Offset Function(Offset offset, Offset delta);

/// No friction. Moves exactly with cursor.
Offset kDragFrictionNone(Offset offset, Offset delta) => delta;

/// Infinite friction. Will not move.
Offset kDragFrictionInfinite(Offset offset, Offset delta) => Offset.zero;

/// The position of the form relative to its target position.
typedef DragCallback = void Function(Offset offset, Velocity velocity);

/// No callback.
void kDragCallbackNone(Offset offset, Velocity velocity) {}
