// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

/// A [State] that manages the ticking part of a ticking simulation for its
/// subclass.
abstract class TickingState<T extends StatefulWidget> extends State<T>
    with TickerProviderStateMixin {
  Ticker _ticker;
  Duration _lastTick;

  /// Returns false if [_ticker] should stop ticking after this tick.
  bool handleTick(double elapsedSeconds);

  /// Starts the [_ticker] ticking.
  void startTicking() {
    if (_ticker?.isTicking ?? false) {
      return;
    }
    _ticker = createTicker(_onTick);
    _lastTick = Duration.zero;
    _ticker.start();
  }

  @override
  void dispose() {
    _ticker?.dispose();
    _ticker = null;
    super.dispose();
  }

  void _onTick(Duration elapsed) {
    if (!mounted) {
      _ticker?.dispose();
      _ticker = null;
      return;
    }
    final double elapsedSeconds =
        (elapsed.inMicroseconds - _lastTick.inMicroseconds) / 1000000.0;
    _lastTick = elapsed;

    setState(() {
      bool continueTicking = handleTick(elapsedSeconds);
      if (!continueTicking) {
        _ticker?.dispose();
        _ticker = null;
      }
    });
  }
}
