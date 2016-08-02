// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/widgets.dart';

const Duration kExitDuration = const Duration(milliseconds: 350);

/// A [Widget] that fades in its [child] and then fades it out
/// when it is exited via [EnterExitTransitionFadingChildState]'s
/// exit function.
class EnterExitTransitionFadingChild extends StatefulWidget {
  final Widget child;

  EnterExitTransitionFadingChild({Key key, this.child}) : super(key: key);

  @override
  EnterExitTransitionFadingChildState createState() =>
      new EnterExitTransitionFadingChildState(
          new AnimationController(duration: kExitDuration));
}

/// [State] for [EnterExitTransitionFadingChild].
class EnterExitTransitionFadingChildState
    extends State<EnterExitTransitionFadingChild> {
  final AnimationController _controller;
  final Animation<double> _alphaAnimation;
  Future<Null> _exitingFuture;

  EnterExitTransitionFadingChildState(AnimationController controller)
      : _controller = controller,
        _alphaAnimation = new Tween<double>(begin: 0.0, end: 1.0).animate(
            new CurvedAnimation(
                parent: controller,
                curve: Curves.fastOutSlowIn,
                reverseCurve: Curves.fastOutSlowIn.flipped)) {
    _controller.forward();
  }

  @override
  Widget build(_) =>
      new FadeTransition(opacity: _alphaAnimation, child: config.child);

  @override
  void dispose() {
    _controller.stop();
    super.dispose();
  }

  /// Starts or continues the exiting transition.  The [Future] returned
  /// will complete when the transition it complete.
  Future<Null> exit() =>
      _exitingFuture ?? (_exitingFuture = _controller.reverse());
}
