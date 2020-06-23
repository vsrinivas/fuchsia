// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:lib.widgets/utils.dart';

void main() {
  ui.PointerData _createSimulatedPointerData(ui.PointerChange change,
      int timeStampUs, double x, double y, double deltaX, double deltaY) {
    return ui.PointerData(
        buttons: 0,
        device: 0,
        timeStamp: Duration(microseconds: timeStampUs),
        change: change,
        kind: ui.PointerDeviceKind.touch,
        physicalDeltaX: deltaX,
        physicalDeltaY: deltaY,
        physicalX: x,
        physicalY: y,
        pointerIdentifier: 1,
        synthesized: false);
  }

  test('resampling', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 50.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 2000, 10.0, 40.0, 10.0, -10.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.move, 3000, 20.0, 30.0, 10.0, -10.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.move, 4000, 30.0, 20.0, 10.0, -10.0);
    final data4 = _createSimulatedPointerData(
        ui.PointerChange.up, 5000, 40.0, 10.0, 10.0, -10.0);
    final data5 = _createSimulatedPointerData(
        ui.PointerChange.remove, 6000, 50.0, 0.0, 10.0, -10.0);

    resampler
      ..addData(data0)
      ..addData(data1)
      ..addData(data2)
      ..addData(data3)
      ..addData(data4)
      ..addData(data5);

    var result = resampler.sample(Duration(microseconds: 500));

    // No pointer data should have been returned yet.
    expect(result.isEmpty, true);

    result = resampler.sample(Duration(microseconds: 1500));

    // Add pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 1500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 5.0);
    expect(result[0].physicalY, 45.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 2500));

    // Down pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 2500));
    expect(result[0].change, ui.PointerChange.down);
    expect(result[0].physicalX, 15.0);
    expect(result[0].physicalY, 35.0);
    expect(result[0].physicalDeltaX, 10.0);
    expect(result[0].physicalDeltaY, -10.0);

    result = resampler.sample(Duration(microseconds: 3500));

    // Move pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 3500));
    expect(result[0].change, ui.PointerChange.move);
    expect(result[0].physicalX, 25.0);
    expect(result[0].physicalY, 25.0);
    expect(result[0].physicalDeltaX, 10.0);
    expect(result[0].physicalDeltaY, -10.0);

    result = resampler.sample(Duration(microseconds: 4500));

    // Up pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 4500));
    expect(result[0].change, ui.PointerChange.up);
    expect(result[0].physicalX, 35.0);
    expect(result[0].physicalY, 15.0);
    expect(result[0].physicalDeltaX, 10.0);
    expect(result[0].physicalDeltaY, -10.0);

    result = resampler.sample(Duration(microseconds: 5500));

    // Remove pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 5500));
    expect(result[0].change, ui.PointerChange.remove);
    expect(result[0].physicalX, 45.0);
    expect(result[0].physicalY, 5.0);
    expect(result[0].physicalDeltaX, 10.0);
    expect(result[0].physicalDeltaY, -10.0);

    result = resampler.sample(Duration(microseconds: 6500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);
  });

  test('quick tap', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 0.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 1000, 0.0, 0.0, 0.0, 0.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.up, 1000, 0.0, 0.0, 0.0, 0.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.remove, 1000, 0.0, 0.0, 0.0, 0.0);

    resampler..addData(data0)..addData(data1)..addData(data2)..addData(data3);

    var result = resampler.sample(Duration(microseconds: 1500));

    // All pointer data should have been returned.
    expect(result.length, 4);
    expect(result[0].timeStamp, Duration(microseconds: 1500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 0.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 1500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 0.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);
    expect(result[2].timeStamp, Duration(microseconds: 1500));
    expect(result[2].change, ui.PointerChange.up);
    expect(result[2].physicalX, 0.0);
    expect(result[2].physicalY, 0.0);
    expect(result[2].physicalDeltaX, 0.0);
    expect(result[2].physicalDeltaY, 0.0);
    expect(result[3].timeStamp, Duration(microseconds: 1500));
    expect(result[3].change, ui.PointerChange.remove);
    expect(result[3].physicalX, 0.0);
    expect(result[3].physicalY, 0.0);
    expect(result[3].physicalDeltaX, 0.0);
    expect(result[3].physicalDeltaY, 0.0);
  });

  test('advance slowly', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 0.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 1000, 0.0, 0.0, 0.0, 0.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.move, 2000, 10.0, 0.0, 10.0, 0.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.up, 3000, 20.0, 0.0, 10.0, 0.0);
    final data4 = _createSimulatedPointerData(
        ui.PointerChange.remove, 3000, 20.0, 0.0, 0.0, 0.0);

    resampler
      ..addData(data0)
      ..addData(data1)
      ..addData(data2)
      ..addData(data3)
      ..addData(data4);

    var result = resampler.sample(Duration(microseconds: 1500));

    // Up and down pointer data should have been returned.
    expect(result.length, 2);
    expect(result[0].timeStamp, Duration(microseconds: 1500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 5.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 1500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 5.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 1500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);

    result = resampler.sample(Duration(microseconds: 1750));

    // Move pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 1750));
    expect(result[0].change, ui.PointerChange.move);
    expect(result[0].physicalX, 7.5);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 2.5);
    expect(result[0].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 2000));

    // Another move pointer data should have been returned.
    expect(result.length, 1);
    expect(result[0].timeStamp, Duration(microseconds: 2000));
    expect(result[0].change, ui.PointerChange.move);
    expect(result[0].physicalX, 10.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 2.5);
    expect(result[0].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 2500));

    // Last two pointer datas should have been returned.
    expect(result.length, 2);
    expect(result[0].timeStamp, Duration(microseconds: 2500));
    expect(result[0].change, ui.PointerChange.up);
    expect(result[0].physicalX, 15.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 5.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 2500));
    expect(result[1].change, ui.PointerChange.remove);
    expect(result[1].physicalX, 15.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);
  });

  test('advance fast', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 0.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 1000, 0.0, 0.0, 0.0, 0.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.move, 2000, 5.0, 0.0, 5.0, 0.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.move, 3000, 20.0, 0.0, 15.0, 0.0);
    final data4 = _createSimulatedPointerData(
        ui.PointerChange.up, 4000, 30.0, 0.0, 10.0, 0.0);
    final data5 = _createSimulatedPointerData(
        ui.PointerChange.remove, 4000, 30.0, 0.0, 0.0, 0.0);

    resampler
      ..addData(data0)
      ..addData(data1)
      ..addData(data2)
      ..addData(data3)
      ..addData(data4)
      ..addData(data5);

    var result = resampler.sample(Duration(microseconds: 2500));

    // Add and down pointer data should have been returned.
    expect(result.length, 2);
    expect(result[0].timeStamp, Duration(microseconds: 2500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 12.5);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 2500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 12.5);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 5500));

    // Up pointer data should have been returned.
    expect(result.length, 2);
    expect(result[0].timeStamp, Duration(microseconds: 5500));
    expect(result[0].change, ui.PointerChange.up);
    expect(result[0].physicalX, 30.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 17.5);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 5500));
    expect(result[1].change, ui.PointerChange.remove);
    expect(result[1].physicalX, 30.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 6500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);
  });

  test('skip', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 0.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 1000, 0.0, 0.0, 0.0, 0.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.move, 2000, 10.0, 0.0, 10.0, 0.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.up, 3000, 10.0, 0.0, 10.0, 0.0);
    final data4 = _createSimulatedPointerData(
        ui.PointerChange.down, 4000, 20.0, 0.0, 10.0, 0.0);
    final data5 = _createSimulatedPointerData(
        ui.PointerChange.up, 5000, 30.0, 0.0, 10.0, 0.0);
    final data6 = _createSimulatedPointerData(
        ui.PointerChange.remove, 5000, 30.0, 0.0, 10.0, 0.0);

    resampler
      ..addData(data0)
      ..addData(data1)
      ..addData(data2)
      ..addData(data3)
      ..addData(data4)
      ..addData(data5)
      ..addData(data6);

    var result = resampler.sample(Duration(microseconds: 1500));

    // Down pointer data should have been returned.
    expect(result.length, 2);
    expect(result[0].timeStamp, Duration(microseconds: 1500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 5.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 1500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 5.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 4500));

    // All remaining pointer data should have been returned.
    expect(result.length, 4);
    expect(result[0].timeStamp, Duration(microseconds: 4500));
    expect(result[0].change, ui.PointerChange.up);
    expect(result[0].physicalX, 25.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 20.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 4500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 25.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);
    expect(result[2].timeStamp, Duration(microseconds: 4500));
    expect(result[2].change, ui.PointerChange.up);
    expect(result[2].physicalX, 25.0);
    expect(result[2].physicalY, 0.0);
    expect(result[2].physicalDeltaX, 0.0);
    expect(result[2].physicalDeltaY, 0.0);
    expect(result[3].timeStamp, Duration(microseconds: 4500));
    expect(result[3].change, ui.PointerChange.remove);
    expect(result[3].physicalX, 25.0);
    expect(result[3].physicalY, 0.0);
    expect(result[3].physicalDeltaX, 0.0);
    expect(result[3].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 5500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);
  });

  test('skip all', () {
    final resampler = PointerDataResampler();
    final data0 = _createSimulatedPointerData(
        ui.PointerChange.add, 1000, 0.0, 0.0, 0.0, 0.0);
    final data1 = _createSimulatedPointerData(
        ui.PointerChange.down, 1000, 0.0, 0.0, 0.0, 0.0);
    final data2 = _createSimulatedPointerData(
        ui.PointerChange.up, 4000, 30.0, 0.0, 30.0, 0.0);
    final data3 = _createSimulatedPointerData(
        ui.PointerChange.remove, 4000, 30.0, 0.0, 0.0, 0.0);

    resampler..addData(data0)..addData(data1)..addData(data2)..addData(data3);

    var result = resampler.sample(Duration(microseconds: 500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);

    result = resampler.sample(Duration(microseconds: 5500));

    // All remaining pointer data should have been returned.
    expect(result.length, 4);
    expect(result[0].timeStamp, Duration(microseconds: 5500));
    expect(result[0].change, ui.PointerChange.add);
    expect(result[0].physicalX, 30.0);
    expect(result[0].physicalY, 0.0);
    expect(result[0].physicalDeltaX, 0.0);
    expect(result[0].physicalDeltaY, 0.0);
    expect(result[1].timeStamp, Duration(microseconds: 5500));
    expect(result[1].change, ui.PointerChange.down);
    expect(result[1].physicalX, 30.0);
    expect(result[1].physicalY, 0.0);
    expect(result[1].physicalDeltaX, 0.0);
    expect(result[1].physicalDeltaY, 0.0);
    expect(result[2].timeStamp, Duration(microseconds: 5500));
    expect(result[2].change, ui.PointerChange.up);
    expect(result[2].physicalX, 30.0);
    expect(result[2].physicalY, 0.0);
    expect(result[2].physicalDeltaX, 0.0);
    expect(result[2].physicalDeltaY, 0.0);
    expect(result[3].timeStamp, Duration(microseconds: 5500));
    expect(result[3].change, ui.PointerChange.remove);
    expect(result[3].physicalX, 30.0);
    expect(result[3].physicalY, 0.0);
    expect(result[3].physicalDeltaX, 0.0);
    expect(result[3].physicalDeltaY, 0.0);

    result = resampler.sample(Duration(microseconds: 6500));

    // No pointer data should have been returned.
    expect(result.isEmpty, true);
  });
}
