// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/gestures.dart' as gestures;
import 'package:flutter/widgets.dart';

enum _DragState {
  ready,
  possible,
  accepted,
}

enum Direction {
  left,
  right,
}

const double _kTouchSlop = 4.0;

class UnidirectionalHorizontalDragGestureRecognizer
    extends DragGestureRecognizer {
  Direction direction;

  /// Create a gesture recognizer for interactions in the horizontal axis.
  UnidirectionalHorizontalDragGestureRecognizer(
      {Object debugOwner, this.direction})
      : super(debugOwner: debugOwner);

  @override
  bool _isFlingGesture(gestures.VelocityEstimate estimate) {
    final double minVelocity = minFlingVelocity ?? gestures.kMinFlingVelocity;
    final double minDistance = minFlingDistance ?? _kTouchSlop;
    return ((direction == Direction.left ? -1.0 : 1.0) *
                estimate.pixelsPerSecond.dx) >
            minVelocity &&
        ((direction == Direction.left ? -1.0 : 1.0) * estimate.offset.dx) >
            minDistance;
  }

  @override
  bool get _hasSufficientPendingDragDeltaToAccept {
    return ((direction == Direction.left ? -1.0 : 1.0) *
            _pendingDragOffset.dx) >
        _kTouchSlop;
  }

  @override
  Offset _getDeltaForDetails(Offset delta) => Offset(delta.dx, delta.dy);

  @override
  double _getPrimaryValueFromOffset(Offset value) => value.dx;

  @override
  String get debugDescription => 'uni directional horizontal drag';
}

abstract class DragGestureRecognizer
    extends gestures.OneSequenceGestureRecognizer {
  /// Initialize the object.
  DragGestureRecognizer({Object debugOwner}) : super(debugOwner: debugOwner);

  /// A pointer has contacted the screen and has begun to move.
  ///
  /// The position of the pointer is provided in the callback's `details`
  /// argument, which is a [DragStartDetails] object.
  GestureDragStartCallback onStart;

  /// A pointer that is in contact with the screen and moving has moved again.
  ///
  /// The distance travelled by the pointer since the last update is provided in
  /// the callback's `details` argument, which is a [DragUpdateDetails] object.
  GestureDragUpdateCallback onUpdate;

  /// A pointer that was previously in contact with the screen and moving is no
  /// longer in contact with the screen and was moving at a specific velocity
  /// when it stopped contacting the screen.
  ///
  /// The velocity is provided in the callback's `details` argument, which is a
  /// [DragEndDetails] object.
  GestureDragEndCallback onEnd;

  /// The minimum distance an input pointer drag must have moved to
  /// to be considered a fling gesture.
  ///
  /// This value is typically compared with the distance traveled along the
  /// scrolling axis. If null then [gestures.kTouchSlop] is used.
  double minFlingDistance;

  /// The minimum velocity for an input pointer drag to be considered fling.
  ///
  /// This value is typically compared with the magnitude of fling gesture's
  /// velocity along the scrolling axis. If null then [gestures.kMinFlingVelocity]
  /// is used.
  double minFlingVelocity;

  /// Fling velocity magnitudes will be clamped to this value.
  ///
  /// If null then [gestures.kMaxFlingVelocity] is used.
  double maxFlingVelocity;

  _DragState _state = _DragState.ready;
  Offset _initialPosition;
  Offset _pendingDragOffset;
  Duration _lastPendingEventTimestamp;

  bool _isFlingGesture(gestures.VelocityEstimate estimate);
  Offset _getDeltaForDetails(Offset delta);
  double _getPrimaryValueFromOffset(Offset value);
  bool get _hasSufficientPendingDragDeltaToAccept;

  final Map<int, gestures.VelocityTracker> _velocityTrackers =
      <int, gestures.VelocityTracker>{};

  @override
  void addPointer(PointerEvent event) {
    startTrackingPointer(event.pointer);
    _velocityTrackers[event.pointer] = gestures.VelocityTracker();
    if (_state == _DragState.ready) {
      _state = _DragState.possible;
      _initialPosition = event.position;
      _pendingDragOffset = Offset.zero;
      _lastPendingEventTimestamp = event.timeStamp;
    } else if (_state == _DragState.accepted) {
      resolve(gestures.GestureDisposition.accepted);
    }
  }

  @override
  void handleEvent(PointerEvent event) {
    assert(_state != _DragState.ready);
    if (!event.synthesized &&
        (event is PointerDownEvent || event is PointerMoveEvent)) {
      final gestures.VelocityTracker tracker = _velocityTrackers[event.pointer];
      assert(tracker != null);
      tracker.addPosition(event.timeStamp, event.position);
    }

    if (event is PointerMoveEvent) {
      final Offset delta = event.delta;
      if (_state == _DragState.accepted) {
        if (onUpdate != null) {
          invokeCallback<void>(
            'onUpdate',
            () => onUpdate(
                  DragUpdateDetails(
                    sourceTimeStamp: event.timeStamp,
                    delta: _getDeltaForDetails(delta),
                    primaryDelta: null,
                    globalPosition: event.position,
                  ),
                ),
          );
        }
      } else {
        _pendingDragOffset += delta;
        _lastPendingEventTimestamp = event.timeStamp;
        if (_hasSufficientPendingDragDeltaToAccept) {
          resolve(gestures.GestureDisposition.accepted);
        }
      }
    }
    stopTrackingIfPointerNoLongerDown(event);
  }

  @override
  void acceptGesture(int pointer) {
    if (_state != _DragState.accepted) {
      _state = _DragState.accepted;
      final Duration timestamp = _lastPendingEventTimestamp;
      _pendingDragOffset = Offset.zero;
      _lastPendingEventTimestamp = null;
      if (onStart != null) {
        invokeCallback<void>(
          'onStart',
          () => onStart(
                DragStartDetails(
                  sourceTimeStamp: timestamp,
                  globalPosition: _initialPosition,
                ),
              ),
        );
      }
    }
  }

  @override
  void rejectGesture(int pointer) {
    stopTrackingPointer(pointer);
  }

  @override
  void didStopTrackingLastPointer(int pointer) {
    if (_state == _DragState.possible) {
      resolve(gestures.GestureDisposition.rejected);
      _state = _DragState.ready;
      return;
    }
    final bool wasAccepted = _state == _DragState.accepted;
    _state = _DragState.ready;
    if (wasAccepted && onEnd != null) {
      final gestures.VelocityTracker tracker = _velocityTrackers[pointer];
      assert(tracker != null);

      final gestures.VelocityEstimate estimate = tracker.getVelocityEstimate();
      if (estimate != null && _isFlingGesture(estimate)) {
        final Velocity velocity =
            Velocity(pixelsPerSecond: estimate.pixelsPerSecond)
                .clampMagnitude(minFlingVelocity ?? gestures.kMinFlingVelocity,
                    maxFlingVelocity ?? gestures.kMaxFlingVelocity);
        invokeCallback<void>(
            'onEnd',
            () => onEnd(DragEndDetails(
                  velocity: velocity,
                  primaryVelocity:
                      _getPrimaryValueFromOffset(velocity.pixelsPerSecond),
                )), debugReport: () {
          return '$estimate; fling at $velocity.';
        });
      } else {
        invokeCallback<void>(
            'onEnd',
            () => onEnd(DragEndDetails(
                  velocity: Velocity.zero,
                  primaryVelocity: 0.0,
                )), debugReport: () {
          if (estimate == null) {
            return 'Could not estimate velocity.';
          }
          return '$estimate; judged to not be a fling.';
        });
      }
    }
    _velocityTrackers.clear();
  }

  @override
  void dispose() {
    _velocityTrackers.clear();
    super.dispose();
  }
}

class UnidirectionalHorizontalGestureDetector extends StatelessWidget {
  const UnidirectionalHorizontalGestureDetector({
    Key key,
    this.child,
    this.direction,
    this.onHorizontalDragStart,
    this.onHorizontalDragUpdate,
    this.onHorizontalDragEnd,
    this.behavior,
    this.excludeFromSemantics = false,
  })  : assert(excludeFromSemantics != null),
        super(key: key);

  final Direction direction;

  /// The widget below this widget in the tree.
  ///
  /// {@macro flutter.widgets.child}
  final Widget child;

  /// A pointer has contacted the screen and has begun to move horizontally.
  final GestureDragStartCallback onHorizontalDragStart;

  /// A pointer that is in contact with the screen and moving horizontally has
  /// moved in the horizontal direction.
  final GestureDragUpdateCallback onHorizontalDragUpdate;

  /// A pointer that was previously in contact with the screen and moving
  /// horizontally is no longer in contact with the screen and was moving at a
  /// specific velocity when it stopped contacting the screen.
  final GestureDragEndCallback onHorizontalDragEnd;

  /// How this gesture detector should behave during hit testing.
  ///
  /// This defaults to [HitTestBehavior.deferToChild] if [child] is not null and
  /// [HitTestBehavior.translucent] if child is null.
  final HitTestBehavior behavior;

  /// Whether to exclude these gestures from the semantics tree. For
  /// example, the long-press gesture for showing a tooltip is
  /// excluded because the tooltip itself is included in the semantics
  /// tree directly and so having a gesture to show it would result in
  /// duplication of information.
  final bool excludeFromSemantics;

  @override
  Widget build(BuildContext context) {
    final Map<Type, GestureRecognizerFactory> gestures =
        <Type, GestureRecognizerFactory>{};

    if (onHorizontalDragStart != null ||
        onHorizontalDragUpdate != null ||
        onHorizontalDragEnd != null) {
      gestures[UnidirectionalHorizontalDragGestureRecognizer] =
          GestureRecognizerFactoryWithHandlers<
              UnidirectionalHorizontalDragGestureRecognizer>(
        () => UnidirectionalHorizontalDragGestureRecognizer(
              direction: direction,
              debugOwner: this,
            ),
        (UnidirectionalHorizontalDragGestureRecognizer instance) {
          instance
            ..direction = direction
            ..onStart = onHorizontalDragStart
            ..onUpdate = onHorizontalDragUpdate
            ..onEnd = onHorizontalDragEnd;
        },
      );
    }

    return RawGestureDetector(
      gestures: gestures,
      behavior: behavior,
      excludeFromSemantics: excludeFromSemantics,
      child: child,
    );
  }
}
