// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:developer' show Timeline;
import 'dart:ui' show window;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../story_shell_impl.dart';
import 'surface_director.dart';

/// This is used for keeping the reference around.
// ignore: unused_element
StoryShellImpl _storyShellImpl;

/// High level class for choosing between presentations
class Mondrian extends StatefulWidget {
  /// Constructor
  const Mondrian({Key key}) : super(key: key);

  @override
  MondrianState createState() => MondrianState();
}

/// State
class MondrianState extends State<Mondrian> {
  @override
  Widget build(BuildContext context) {
    _traceFrame();
    return SurfaceDirector();
  }
}

int _frameCounter = 1;
void _traceFrame() {
  Size size = window.physicalSize / window.devicePixelRatio;
  Timeline.instantSync('building, size: $size');
  SchedulerBinding.instance.addPostFrameCallback(_frameCallback);
}

void _frameCallback(Duration duration) {
  Size size = window.physicalSize / window.devicePixelRatio;
  Timeline.instantSync('frame $_frameCounter, size: $size');
  _frameCounter++;
  if (size.isEmpty) {
    SchedulerBinding.instance.addPostFrameCallback(_frameCallback);
  }
}
