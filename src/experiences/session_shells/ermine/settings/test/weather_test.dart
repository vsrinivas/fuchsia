// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';

void main() {
  test('Default Weather Spec', () async {
    Weather weather = Weather();

    // Inject mock weather data to guarantee spec has data needed to construct
    var mockLocations = weather.locations;
    for (var location in mockLocations) {
      location
        ..observation = 'testing...'
        ..tempInDegrees = 0;
    }
    weather.locations = mockLocations;

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

      // Set spec to use celsius
      ..tempInFahrenheit = false;

    // Toggle celsius off
    Value mockAction = Value.withButton(ButtonValue(label: 'mock', action: 2));
    weather.update(mockAction);

    // Inject mock weather data to guarantee spec has data needed to construct
    var mockLocations = weather.locations;
    for (var location in mockLocations) {
      location
        ..observation = 'testing...'
        ..tempInDegrees = 0;
    }
    weather.locations = mockLocations;

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

      // Set spec to use fahrenheit
      ..tempInFahrenheit = true;

    // Toggle fahrenheit off
    Value mockAction = Value.withButton(ButtonValue(label: 'mock', action: 2));
    weather.update(mockAction);

    // Inject mock weather data to guarantee spec has data needed to construct
    var mockLocations = weather.locations;
    for (var location in mockLocations) {
      location
        ..observation = 'testing...'
        ..tempInDegrees = 0;
    }
    weather.locations = mockLocations;

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
