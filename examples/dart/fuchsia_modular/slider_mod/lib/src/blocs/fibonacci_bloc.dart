// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'bloc_provider.dart';

/// The [FibonacciBloc] provides actions and streams associated with
/// the Fibonacci agent.
class FibonacciBloc implements BlocBase {
  final _valueController = StreamController<int>.broadcast();
  int _lastKnownValue = 0;

  Stream<int> get valueStream => _valueController.stream;
  int get currentValue => _lastKnownValue;

  void updateValue(int newValue) {
    _lastKnownValue = newValue;
    _valueController.add(newValue);
  }

  @override
  void dispose() {
    _valueController.close();
  }
}
