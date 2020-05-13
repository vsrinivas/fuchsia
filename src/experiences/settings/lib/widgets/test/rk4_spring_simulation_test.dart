// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.widgets/widgets.dart';
import 'package:test/test.dart';

void main() {
  test('initial value', () {
    final RK4SpringSimulation x = RK4SpringSimulation(initValue: 0.0);
    expect(x.value, equals(0.0));
  });

  test('interim value', () {
    final RK4SpringSimulation x = RK4SpringSimulation(initValue: 0.0);
    expect(x.value, equals(0.0));
    x.target = 1.0;
    expect(x.value, equals(0.0));
    x.elapseTime(0.1);
    expect(x.value, greaterThan(0.0));
    expect(x.value, lessThan(1.0));
  });

  test('final value', () {
    final RK4SpringSimulation x = RK4SpringSimulation(initValue: 0.0);
    expect(x.value, equals(0.0));
    x.target = 1.0;
    expect(x.value, equals(0.0));
    x.elapseTime(1.0);
    expect(x.value, equals(1.0));
  });

  test('more friction takes longer to complete', () {
    final RK4SpringSimulation x1 = RK4SpringSimulation(
        desc: RK4SpringDescription(tension: 100.0, friction: 10.0));
    final RK4SpringSimulation x2 = RK4SpringSimulation(
        desc: RK4SpringDescription(tension: 100.0, friction: 50.0));
    x1.target = 100.0;
    x2.target = 100.0;
    x1.elapseTime(1.0);
    x2.elapseTime(1.0);
    expect(x2.value, lessThan(x1.value));
  });

  test('less tension takes longer to complete', () {
    final RK4SpringSimulation x1 = RK4SpringSimulation(
        desc: RK4SpringDescription(tension: 1000.0, friction: 50.0));
    final RK4SpringSimulation x2 = RK4SpringSimulation(
        desc: RK4SpringDescription(tension: 500.0, friction: 50.0));
    x1.target = 100.0;
    x2.target = 100.0;
    x1.elapseTime(0.1);
    x2.elapseTime(0.1);
    expect(x2.value, lessThan(x1.value));
  });
}
