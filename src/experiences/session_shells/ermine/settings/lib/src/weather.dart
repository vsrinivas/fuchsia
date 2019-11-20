// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

const _weatherBaseUrl = 'https://api.weather.gov';

/// Defines a [UiSpec] for displaying weather for locations.
class Weather extends UiSpec {
  // Localized strings.
  static String get _title => Strings.weather;

  static const changeUnitAction = 2;

  // TODO(sanjayc): Replace hardcoded locations with user specified ones.
  static final List<_Location> _locations = <_Location>[
    _Location('Mountain View', station: 'KSJC'),
    _Location('San Francisco', station: 'KSFO'),
  ];

  // Weather refresh duration.
  static final _refreshDuration = Duration(minutes: 10);
  Timer _timer;
  bool tempInFahrenheit = true;

  Weather() {
    _timer = Timer.periodic(_refreshDuration, (_) => _onChange());
    _onChange();
  }

  void _onChange() async {
    try {
      await _refresh();
      spec = _specForWeather(tempInFahrenheit);
    } on Exception catch (_) {
      spec = null;
    }
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button &&
        value.button.action == changeUnitAction) {
      tempInFahrenheit = !tempInFahrenheit;
      spec = _specForWeather(tempInFahrenheit);
    }
  }

  @override
  void dispose() {
    _timer.cancel();
  }

  static Spec _specForWeather(bool tempInFahrenheit) {
    final locations = _locations
        .where((location) =>
            location.observation != null && location.tempInDegrees != null)
        .toList();
    if (locations.isEmpty) {
      return UiSpec.nullSpec;
    }
    return Spec(title: _title, groups: [
      Group(title: _title, values: [
        if (tempInFahrenheit)
          Value.withButton(
              ButtonValue(label: 'Use °C', action: changeUnitAction)),
        if (!tempInFahrenheit)
          Value.withButton(
              ButtonValue(label: 'Use °F', action: changeUnitAction)),
        Value.withGrid(GridValue(
            columns: 2,
            values: List<TextValue>.generate(locations.length * 2, (index) {
              final location = locations[index ~/ 2];
              final temp =
                  tempInFahrenheit ? location.fahrenheit : location.degrees;
              final weather = '$temp / ${location.observation}';
              return TextValue(text: index.isEven ? location.name : weather);
            }))),
      ]),
    ]);
  }

  static Future<void> _refresh() async {
    await Future.forEach(_locations, _loadCurrentCondition);
  }

  // Get the latest observation for weather station in [_Location] using:
  // https://www.weather.gov/documentation/services-web-api#/default/get_points__point__stations
  static Future<void> _loadCurrentCondition(_Location location) async {
    final observationUrl =
        '$_weatherBaseUrl/stations/${location.station}/observations/latest';
    var request = await HttpClient().getUrl(Uri.parse(observationUrl));
    var response = await request.close();
    var result = await _readResponse(response);
    var data = json.decode(result);
    var properties = data['properties'];
    if (properties['textDescription'] != null &&
        properties['temperature'] != null &&
        properties['temperature']['value'] != null) {
      location
        ..observation = properties['textDescription']
        ..tempInDegrees = properties['temperature']['value'].toDouble();
    }
  }

  // Read the string response from the [HttpClientResponse].
  static Future<String> _readResponse(HttpClientResponse response) {
    var completer = Completer<String>();
    var contents = StringBuffer();
    response.transform(utf8.decoder).listen((data) {
      contents.write(data);
    }, onDone: () => completer.complete(contents.toString()));
    return completer.future;
  }
}

// Holds weather data for a location.
class _Location {
  final String name;
  final String point;
  String observation;
  String station;
  double tempInDegrees;

  _Location(this.name, {this.point, this.station, this.observation});

  String get degrees => '${tempInDegrees.toInt()}°C';

  String get fahrenheit => '${(tempInDegrees * 1.8 + 32).toInt()}°F';

  @override
  String toString() =>
      'name: $name station: $station observation: $observation temp: $tempInDegrees';
}
