// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/scheduler.dart';
import 'model.dart';

export 'model.dart' show ScopedModel, Model, ScopedModelDescendant;

/// Base class for [Model]s that depend on a Ticker.
abstract class TickingModel extends Model {
  Ticker _ticker;
  Duration _lastTick;

  /// Returns false if [_ticker] should stop ticking after this tick.
  bool handleTick(double elapsedSeconds);

  /// Starts the [_ticker].
  void startTicking() {
    if (_ticker?.isTicking ?? false) {
      return;
    }
    if (handleTick(0.0)) {
      _ticker = Ticker(_onTick);
      _lastTick = Duration.zero;
      _ticker.start();
    }
  }

  void _onTick(Duration elapsed) {
    final double elapsedSeconds =
        (elapsed.inMicroseconds - _lastTick.inMicroseconds) / 1000000.0;
    _lastTick = elapsed;

    bool continueTicking = handleTick(elapsedSeconds);
    if (!continueTicking) {
      _ticker?.dispose();
      _ticker = null;
    }
    notifyListeners();
  }
}
