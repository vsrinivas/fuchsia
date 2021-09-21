// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'bloc_provider.dart';

/// The [SliderBloc] provides actions and streams associated with
/// the onscreen slider.
class SliderBloc implements BlocBase {
  final _valueController = StreamController<double>.broadcast();
  final double minValue = 0.0;
  final double maxValue = 10.0;

  double _currentValue = 0.0;

  SliderBloc();

  double get currentValue => _currentValue;
  Stream<double> get valueStream => _valueController.stream;

  @override
  void dispose() {
    _valueController.close();
  }

  void updateValue(double newValue) {
    _currentValue = newValue;
    _valueController.add(newValue);
  }
}
