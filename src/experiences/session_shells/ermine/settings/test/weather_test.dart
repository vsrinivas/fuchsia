// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';

void main() {
  test('Default Weather Spec', () async {
    Weather weather = Weather();

    // Should receive weather spec
    Spec spec = await weather.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm grid of locations present
    Iterable grid =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.grid);
    expect(grid.isEmpty, isFalse);

    // Confirm celsius/fahrenheit button present
    bool hasButton =
        spec.groups.first.values.any((v) => v.$tag == ValueTag.button);
    expect(hasButton, isTrue);
  });

  test('Toggle Celsius', () async {
    Weather weather = Weather()

      // Toggle celsius off
      ..tempInFahrenheit = true;

    // Should receive weather spec
    Spec spec = await weather.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm 'use celsius' button present
    Iterable button =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(button.first.button.label, 'Use °C');
  });

  test('Toggle Fahrenheit', () async {
    Weather weather = Weather()

      // Toggle fahrenheit off
      ..tempInFahrenheit = false;

    // Should receive weather spec
    Spec spec = await weather.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm 'use fahrenheit' button present
    Iterable button =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(button.first.button.label, 'Use °F');
  });
}
