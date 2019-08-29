// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:meta/meta.dart';

import 'sim.dart';

/// An Animation<T> that carries an intrinsic velocity.
///
/// Having velocity is useful in making non-jerky animation transitions.
abstract class FluxAnimation<T> extends Animation<T> {
  /// Default constructor
  const FluxAnimation();

  /// Wrap an Animation as a FluxAnimation. Will
  factory FluxAnimation.fromAnimation(Animation<T> animation, T velocity) =>
      animation is FluxAnimation
          ? animation
          : _FluxAnimationWrapper<T>(animation, velocity);

  /// The instantaneous change in the value in natural units per second.
  T get velocity;
}

/// A function which returns a FluxAnimation from an initial value and velocity.
typedef FluxAnimationInit<T> = FluxAnimation<T> Function(T value, T velocity);

class _FluxAnimationWrapper<T> extends FluxAnimation<T> {
  _FluxAnimationWrapper(this.animation, this._velocity)
      : assert(animation != null),
        assert(_velocity != null);
  final Animation<T> animation;
  final T _velocity;

  @override
  AnimationStatus get status => animation.status;

  @override
  T get value => animation.value;

  @override
  T get velocity => _velocity;

  @override
  void addListener(VoidCallback listener) => animation.addListener(listener);

  @override
  void removeListener(VoidCallback listener) =>
      animation.removeListener(listener);

  @override
  void addStatusListener(AnimationStatusListener listener) =>
      animation.addStatusListener(listener);

  @override
  void removeStatusListener(AnimationStatusListener listener) =>
      animation.removeStatusListener(listener);
}

/// A FluxAnimation provided by a Sim.
class SimAnimationController<T> extends FluxAnimation<T>
    with
        AnimationLocalStatusListenersMixin,
        AnimationLocalListenersMixin,
        AnimationEagerListenerMixin {
  /// Create a SimAnimation with given Sim
  SimAnimationController({
    @required TickerProvider vsync,
    @required this.sim,
  })  : _value = sim.value(0.0),
        _velocity = sim.velocity(0.0),
        _elapsed = Duration.zero,
        _elapsedOffset = Duration.zero {
    _ticker = vsync.createTicker(_tick);
  }

  /// The Sim for this animation.
  final Sim<T> sim;

  Ticker _ticker;

  @override
  AnimationStatus get status => _ticker.isActive
      ? AnimationStatus.forward
      : sim.isDone(_elapsedInSeconds)
          ? AnimationStatus.completed
          : AnimationStatus.dismissed;

  @override
  T get value => _value;
  T _value;

  @override
  T get velocity => _velocity;
  T _velocity;

  /// The elapsed duration for this simulation. Stops animation if manually set.
  Duration get elapsed => _elapsed + _elapsedOffset;
  Duration _elapsed;
  Duration _elapsedOffset;
  set elapsed(Duration duration) {
    assert(duration != null);
    stop();
    _elapsedOffset = duration;
    _tick(Duration.zero);
  }

  double get _elapsedInSeconds =>
      (_elapsed + _elapsedOffset).inMicroseconds.toDouble() /
      Duration.microsecondsPerSecond;

  /// Start the animation.
  TickerFuture start() {
    TickerFuture future = _ticker.start()
      ..whenCompleteOrCancel(_sendStatusUpdate);
    _sendStatusUpdate();
    return future;
  }

  /// Stop the animation, optionally marking it as cancelled.
  void stop({bool canceled = false}) {
    _ticker.stop(canceled: canceled);
    _elapsedOffset = elapsed;
    _elapsed = Duration.zero;
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  void _tick(Duration elapsed) {
    _elapsed = elapsed;
    double elapsedSeconds = _elapsedInSeconds;
    _value = sim.value(elapsedSeconds);
    _velocity = sim.velocity(elapsedSeconds);
    if (sim.isDone(elapsedSeconds)) {
      _ticker.stop();
    }
    notifyListeners();
  }

  AnimationStatus _lastSentStatus;
  void _sendStatusUpdate() {
    if (status != _lastSentStatus) {
      _lastSentStatus = status;
      notifyStatusListeners(status);
    }
  }
}

/// A manually controllable FluxAnimation
///
/// The value and velocity of this animation can be manually controlled, also
/// a secondary delegate animation can be provided to continue the animation
/// upon completion.
class ManualAnimation<T> extends FluxAnimation<T>
    with
        AnimationLocalStatusListenersMixin,
        AnimationLocalListenersMixin,
        AnimationLazyListenerMixin {
  /// Constructs with initial value and velocity.
  ManualAnimation({
    @required T value,
    @required T velocity,
    FluxAnimationInit<T> builder,
  })  : _value = value,
        _velocity = velocity,
        _builder = builder,
        _delegate = null,
        _done = false,
        assert(value != null),
        assert(velocity != null);

  /// Constructs with value and velocity provided by delegate to start.
  ManualAnimation.withDelegate({@required FluxAnimation<T> delegate})
      : _delegate = delegate,
        _builder = null,
        _done = true,
        assert(delegate != null);

  final FluxAnimationInit<T> _builder;
  FluxAnimation<T> _delegate;

  @override
  T get value => _done ? (_delegate?.value ?? _value) : _value;
  T _value;

  @override
  T get velocity => _done ? (_delegate?.velocity ?? _velocity) : _velocity;
  T _velocity;

  bool _done;

  @override
  AnimationStatus get status => _done
      ? (_delegate?.status ?? AnimationStatus.completed)
      : AnimationStatus.forward;

  /// Manually change the value and velocity of this animation
  void update({@required T value, @required T velocity}) {
    assert(value != null);
    assert(velocity != null);
    AnimationStatus oldStatus = status;
    if (_done) {
      _stopDelegating();
      _done = false;
    }
    _value = value;
    _velocity = velocity;
    if (oldStatus != status) {
      notifyStatusListeners(status);
    }
    notifyListeners();
  }

  /// Signal the end of manually changing this animation.
  ///
  /// This either returns control back to the delegate, or signals an
  /// AnimationStatus.complete if no delegate is provided.
  void done() {
    if (_done) {
      return;
    }
    AnimationStatus oldStatus = status;
    _done = true;
    _startDelegating();
    if (status != oldStatus) {
      notifyStatusListeners(status);
    }
    notifyListeners();
  }

  @override
  void didStartListening() {
    _startDelegating();
  }

  @override
  void didStopListening() {
    _stopDelegating();
  }

  void _startDelegating() {
    if (_done) {
      if (_builder != null) {
        _delegate = _builder(_value, _velocity);
      }
      _delegate?.addListener(notifyListeners);
      _delegate?.addStatusListener(notifyStatusListeners);
    }
  }

  void _stopDelegating() {
    if (_done) {
      _delegate?.removeListener(notifyListeners);
      _delegate?.removeStatusListener(notifyStatusListeners);
    }
  }
}

/// Animation that animates to the target value according to the given Simulate.
class MovingTargetAnimation<T> extends FluxAnimation<T>
    with
        AnimationLocalStatusListenersMixin,
        AnimationLocalListenersMixin,
        AnimationLazyListenerMixin {
  /// Constructs using the provided target, simulate, and initial values.
  MovingTargetAnimation({
    @required TickerProvider vsync,
    @required Animation<T> target,
    @required this.simulate,
    @required T value,
    @required T velocity,
  })  : _vsync = vsync,
        assert(target != null),
        target = FluxAnimation<T>.fromAnimation(target, velocity),
        assert(vsync != null),
        assert(simulate != null) {
    _value = value;
    _velocity = velocity;
  }

  /// The simulation generator for moving to target.
  final Simulate<T> simulate;

  /// The moving target.
  final FluxAnimation<T> target;

  final TickerProvider _vsync;
  SimAnimationController<T> _animation;
  T _value;
  T _velocity;
  int _lastUpdateCallbackId;

  @override
  AnimationStatus get status =>
      _animation?.status ??
      (simulate(value, target.value, velocity).isDone(0.0)
          ? AnimationStatus.completed
          : AnimationStatus.forward);

  @override
  T get value => _animation?.value ?? _value;

  @override
  T get velocity => _animation?.velocity ?? _velocity;

  /// Like this animation except stays in lockstep with target once reached.
  FluxAnimation<T> get stickyAnimation => target.value == _value
      // HACK(alangardner): Using '==' is fragile. We should instead use
      // simulate(value, target.value, velocityOrigin).isDone(0.0)
      // where velocityOrigin is the 'zero' velocity. However, this introduces
      // a zeroVelocity parameter complexity to the API and may be confusing.
      ? target
      : ChainedAnimation<T>(this, then: target);

  void _scheduleUpdate() {
    if (_lastUpdateCallbackId != null) {
      SchedulerBinding.instance
          .cancelFrameCallbackWithId(_lastUpdateCallbackId);
    }
    _lastUpdateCallbackId =
        SchedulerBinding.instance.scheduleFrameCallback((Duration timestamp) {
      _update();
      _lastUpdateCallbackId = null;
    });
  }

  void _update() {
    Sim<T> sim = simulate(value, target.value, velocity);
    _disposeAnimation();
    _animation = SimAnimationController<T>(vsync: _vsync, sim: sim)
      ..addListener(notifyListeners)
      ..addStatusListener(notifyStatusListeners);
    if (!_animation.isCompleted) {
      _animation.start();
    }
  }

  void _disposeAnimation() {
    if (_animation != null) {
      _value = _animation.value;
      _velocity = _animation.velocity;
      _animation
        ..removeListener(notifyListeners)
        ..removeStatusListener(notifyStatusListeners)
        ..dispose();
      _animation = null;
    }
  }

  @override
  void didStartListening() {
    _update();
    target.addListener(_scheduleUpdate);
  }

  @override
  void didStopListening() {
    target.removeListener(_scheduleUpdate);
    _disposeAnimation();
    if (_lastUpdateCallbackId != null) {
      SchedulerBinding.instance
          .cancelFrameCallbackWithId(_lastUpdateCallbackId);
      _lastUpdateCallbackId = null;
    }
  }
}

/// A generic transform function from one value to another of the same type.
typedef Transform<T> = T Function(T value);

/// A transformation wrapper for an animation.
class TransformedAnimation<T> extends FluxAnimation<T>
    with
        AnimationLocalStatusListenersMixin,
        AnimationLocalListenersMixin,
        AnimationLazyListenerMixin {
  /// Construct using a delegate animation and tranformation function.
  TransformedAnimation({
    @required this.animation,
    @required this.valueTransform,
    @required this.velocityTransform,
  });

  /// The delegate animation.
  final FluxAnimation<T> animation;

  /// The value transform function.
  final Transform<T> valueTransform;

  /// The velocity transform function.
  final Transform<T> velocityTransform;

  @override
  AnimationStatus get status => animation.status;

  @override
  T get value => valueTransform(animation.value);

  @override
  T get velocity => velocityTransform(animation.velocity);

  @override
  void didStartListening() {
    animation
      ..addListener(notifyListeners)
      ..addStatusListener(notifyStatusListeners);
  }

  @override
  void didStopListening() {
    animation
      ..removeListener(notifyListeners)
      ..removeStatusListener(notifyStatusListeners);
  }
}

/// A FluxAnimation that chains flux animations in succession.
///
/// First the initial animation is active until its status turns to Completed,
/// then the next animation becomes active. If any animation along the way is
/// dismissed, then it stops the chain until it completes.
///
/// If a previously completed animation in the chain changes status, this only
/// effects the chained animation if it is the final animation in the chain.
class ChainedAnimation<T> extends FluxAnimation<T>
    with
        AnimationLocalStatusListenersMixin,
        AnimationLocalListenersMixin,
        AnimationLazyListenerMixin {
  /// Construct a chained animation starting with animation followed by then.
  ChainedAnimation(this._animation, {FluxAnimation<T> then})
      : _next = (then == null) ? null : ChainedAnimation<T>(then) {
    _active = (_next != null && _animation.isCompleted) ? _next : _animation;
  }

  final FluxAnimation<T> _animation;
  final ChainedAnimation<T> _next;
  FluxAnimation<T> _active;

  /// Returns a new FluxAnimation that runs the current animation,
  /// followed by the one specified in the argument when it is complete.
  ChainedAnimation<T> then(FluxAnimation<T> next) =>
      ChainedAnimation<T>(_animation, then: next);

  @override
  AnimationStatus get status => _active.status;

  @override
  T get value => _active.value;

  @override
  T get velocity => _active.velocity;

  AnimationStatusListener get _currentStatusListener =>
      _active == _animation && _next != null
          ? _animationStatusListener
          : notifyStatusListeners;

  void _animationStatusListener(AnimationStatus status) {
    if (_active == _animation &&
        _next != null &&
        (_animation.isCompleted || _animation.isDismissed)) {
      _animation
        ..removeListener(notifyListeners)
        ..removeStatusListener(_animationStatusListener);
      _active = _next
        ..addListener(notifyListeners)
        ..addStatusListener(notifyStatusListeners);
    }
    if (status != AnimationStatus.completed) {
      notifyStatusListeners(status);
    }
  }

  @override
  void didStartListening() {
    _active = (((_animation.isCompleted || _animation.isDismissed)
            ? _next
            : _animation) ??
        _animation)
      ..addListener(notifyListeners)
      ..addStatusListener(_currentStatusListener);
  }

  @override
  void didStopListening() {
    _active
      ..removeListener(notifyListeners)
      ..removeStatusListener(_currentStatusListener);
  }
}
