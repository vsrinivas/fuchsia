// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:flutter/widgets.dart';
import 'package:flutter/material.dart' show Colors;

/// A widget that 'breaths' from grey to white, to use as a placeholder
/// for incoming Surfaces. The transition begins with a pause for _kPausePeriod,
/// then a forward curve for _kBreathPeriod, then a pause, then a reverse curve
/// modulating opacity _kTopColor between  _kLowerBound and _kUpperBound -
/// the effect is the basecolor bleeds through in a breathing motion.
const Color _kBaseColor = Colors.white;
final Color _kTopColor = Colors.grey[200];
const _kBreathPeriod = 300; //ms
const _kPausePeriod = 400; //ms
const double _kLowerBound = 0.2;
const double _kUpperBound = 0.8;

class BreathingPlaceholder extends StatefulWidget {
  @override
  BreathingState createState() => BreathingState();
}

class BreathingState extends State<BreathingPlaceholder>
    with TickerProviderStateMixin {
  AnimationController _opacityController;
  Animation _opacity;
  Timer _timer;
  bool _forward = false;

  @override
  void initState() {
    super.initState();
    pause();
    _opacityController = AnimationController(
      vsync: this,
      duration: Duration(milliseconds: _kBreathPeriod),
      lowerBound: _kLowerBound,
      upperBound: _kUpperBound,
    );
    _opacity = CurvedAnimation(
      parent: _opacityController,
      curve: Curves.easeIn,
    )..addStatusListener(
        (AnimationStatus status) {
          if (status == AnimationStatus.completed) {
            pause();
          }
        },
      );
  }

  void pause() {
    _timer = Timer(Duration(milliseconds: _kPausePeriod), animate);
  }

  void animate() {
    if (_forward) {
      // check for null in case timer happens to callback after dispose()
      _opacityController?.reverse();
      _forward = false;
    } else {
      _opacityController?.forward();
      _forward = true;
    }
  }

  @override
  void dispose() {
    super.dispose();
    _opacityController.dispose();
  }

  @override
  void deactivate() {
    super.deactivate();
    _timer.cancel();
    _opacityController.stop();
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: <Widget>[
        Container(
          color: _kBaseColor,
        ),
        FadeTransition(
          child: Container(
            color: _kTopColor,
          ),
          opacity: _opacity,
        ),
      ],
    );
  }
}
