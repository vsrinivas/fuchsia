// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines a widget that allows driving an implicit animation.
///
/// The [tween] member holds the begin and end values of the animation.
/// The [builder] is invoked for every step of the animation and supplies the
/// actual animation.
class AnimationDriver<T> extends StatefulWidget {
  final Tween<T> tween;
  final Duration duration;
  final Curve curve;
  final bool forward;
  final Widget Function(BuildContext context, Animation<T> animation) builder;
  final VoidCallback onComplete;

  const AnimationDriver({
    @required this.tween,
    @required this.builder,
    this.duration = const Duration(milliseconds: 250),
    this.curve = Curves.linear,
    this.forward = true,
    this.onComplete,
  });

  @override
  _AnimationDriverState<T> createState() => _AnimationDriverState<T>();
}

class _AnimationDriverState<T> extends State<AnimationDriver<T>>
    with SingleTickerProviderStateMixin {
  AnimationController _controller;
  Animation<T> _animation;

  @override
  void initState() {
    super.initState();

    _controller = AnimationController(vsync: this, duration: widget.duration)
      ..addListener(() => setState(() {}))
      ..addStatusListener((status) => status == AnimationStatus.completed
          ? WidgetsBinding.instance.addPostFrameCallback((_) {
              widget.onComplete?.call();
            })
          : {});

    _animate();
  }

  @override
  void didUpdateWidget(Widget oldWidget) {
    super.didUpdateWidget(oldWidget);

    _controller.duration = widget.duration;

    _animate();
  }

  void _animate() {
    _animation =
        _controller.drive(CurveTween(curve: widget.curve)).drive(widget.tween);
    widget.forward ? _controller.forward() : _controller.reverse();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return widget.builder(context, _animation);
  }
}
