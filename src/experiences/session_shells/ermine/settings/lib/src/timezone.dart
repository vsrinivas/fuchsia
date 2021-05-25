// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:async/async.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for displaying and changing timezone.
class TimeZone extends UiSpec {
  // Localized strings.
  static String get _title => Strings.timezone;

  // Icon for timezone title.
  static IconValue get _icon =>
      IconValue(codePoint: Icons.access_time.codePoint);

  // Action to change timezone.
  static int changeAction = QuickAction.details.$value;

  late _TimeZoneModel model;
  Future<List<TimeZoneInfo>> Function() timeZonesProvider;

  TimeZone({
    required IntlProxy intlSettingsService,
    required this.timeZonesProvider,
  }) {
    model = _TimeZoneModel(
      intlSettingsService: intlSettingsService,
      onChange: _onChange,
    );
  }

  factory TimeZone.withSvcPath() {
    final intlSettingsService = IntlProxy();
    Incoming.fromSvcPath().connectToService(intlSettingsService);

    final timeZonesLoader = _TimeZonesLoader();

    final timezone = TimeZone(
        intlSettingsService: intlSettingsService,
        timeZonesProvider: timeZonesLoader.getList);
    return timezone;
  }

  void _onChange() async {
    spec = await _specForTimeZone(model);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button &&
        value.button!.action == QuickAction.cancel.$value) {
      spec = await _specForTimeZone(model);
    } else if (value.$tag == ValueTag.text && value.text!.action > 0) {
      if (value.text!.action == changeAction) {
        spec = await _specForTimeZone(model, changeAction);
      } else {
        final index = value.text!.action ^ QuickAction.submit.$value;
        model.timeZoneId = (await timeZonesProvider())[index].zoneId;
        spec = await _specForTimeZone(model);
      }
    }
  }

  @override
  void dispose() {
    model.dispose();
  }

  Future<Spec> _specForTimeZone(_TimeZoneModel model, [int action = 0]) async {
    if (action == 0 || action & QuickAction.cancel.$value > 0) {
      return Spec(title: _title, groups: [
        Group(title: _title, icon: _icon, values: [
          Value.withText(TextValue(
            text: model.timeZoneId!,
            action: changeAction,
          )),
          Value.withIcon(IconValue(
            codePoint: Icons.arrow_right.codePoint,
            action: changeAction,
          )),
        ]),
      ]);
    } else if (action == changeAction) {
      var timeZones = await timeZonesProvider();
      final values = List<TextValue>.generate(
          timeZones.length,
          (index) => TextValue(
                text: timeZones[index].zoneId,
                action: QuickAction.submit.$value | index,
              ));
      return Spec(title: _title, groups: [
        Group(title: 'Select Timezone', values: [
          Value.withGrid(GridValue(
            columns: 1,
            values: values,
          )),
          Value.withButton(ButtonValue(
            label: 'close',
            action: QuickAction.cancel.$value,
          )),
        ]),
      ]);
    } else {
      return Spec(title: _title, groups: [
        Group(title: _title, values: [
          Value.withText(TextValue(
            text: model.timeZoneId!,
            action: changeAction,
          )),
        ]),
      ]);
    }
  }
}

class _TimeZoneModel {
  IntlProxy intlSettingsService;
  final VoidCallback onChange;

  IntlSettings? _intlSettings;

  _TimeZoneModel({required this.intlSettingsService, required this.onChange}) {
    // Get current timezone and watch it for changes.
    intlSettingsService.watch().then(_onIntlSettingsChange);
  }

  Future<void> _onIntlSettingsChange(IntlSettings intlSettings) async {
    bool timeZoneChanged = (_intlSettings == null) ||
        (_intlSettings?.timeZoneId?.id != intlSettings.timeZoneId?.id);
    _intlSettings = intlSettings;
    if (timeZoneChanged) {
      onChange();
    }
    // Use the FIDL "hanging get" pattern to request the next update.
    await intlSettingsService.watch().then(_onIntlSettingsChange);
  }

  void dispose() {
    intlSettingsService.ctrl.close();
  }

  String? get timeZoneId =>
      _intlSettings == null ? null : _intlSettings?.timeZoneId?.id;
  set timeZoneId(String? value) {
    final IntlSettings newIntlSettings = IntlSettings(
        locales: _intlSettings?.locales,
        temperatureUnit: _intlSettings?.temperatureUnit,
        timeZoneId: TimeZoneId(id: value!));
    intlSettingsService.set(newIntlSettings);
  }
}

// Information needed to render a time zone list entry.
class TimeZoneInfo {
  /// The ICU standard zone ID.
  final String zoneId;

  const TimeZoneInfo({required this.zoneId});
}

// Loads and caches a list of time zones from the Ermine package.
class _TimeZonesLoader {
  final _memoizer = AsyncMemoizer<List<TimeZoneInfo>>();

  Future<List<TimeZoneInfo>> getList() async => _memoizer.runOnce(_loadList);

  Future<List<TimeZoneInfo>> _loadList() async {
    var file = File('/pkg/data/tz_ids.txt');
    List<TimeZoneInfo> timeZones = (await file.readAsLines())
        .map((id) => TimeZoneInfo(zoneId: id))
        .toList();
    return timeZones;
  }
}
