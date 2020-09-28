// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import 'spinning_cube_gem.dart';

class _TickerProviderImpl implements TickerProvider {
  @override
  Ticker createTicker(TickerCallback onTick) => Ticker(onTick);
}

const Duration _kCubeRotationAnimationPeriod = Duration(
  milliseconds: 12000,
);

void main() {
  AnimationController controller = AnimationController(
    vsync: _TickerProviderImpl(),
    duration: _kCubeRotationAnimationPeriod,
  );
  runApp(
    MaterialApp(
      home: Container(
        color: Colors.deepPurple,
        child: FractionallySizedBox(
          alignment: FractionalOffset.center,
          widthFactor: 0.75,
          heightFactor: 0.75,
          child: Center(
            child: SpinningCubeGem(
              controller: controller,
              color: Colors.pinkAccent[400],
            ),
          ),
        ),
      ),
    ),
  );
  controller.repeat();
}
