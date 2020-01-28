// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart' hide Intent;

import 'models/embedded_module.dart';
import 'models/settings_model.dart';

/// [SettingEntry] defines a setting that can be displayed and launched from
/// main landing page. Aside from the id, consumers of this class should not
/// access to any internal state.
abstract class SettingEntry {
  /// Returns the unique identifier for this setting. Care should be taken to
  /// not have collisions on this field.
  String get id;

  /// The route to access the setting using the Navigator class.
  String get route;

  /// Adds entry point for setting through an embedded module.
  Future<EmbeddedModule> embedModule();

  /// Returns a rendering of the setting.
  Widget getRow(SettingsModel model, BuildContext context);

  /// Constructs the route action for the module.
  WidgetBuilder getRouteBuilder(SettingsModel model);
}

/// The license page is built into flutter (rather than being a separate mod) so
/// we handle it separately.
class LicenseSettingEntry implements SettingEntry {
  @override
  Future<EmbeddedModule> embedModule() {
    return null;
  }

  @override
  WidgetBuilder getRouteBuilder(SettingsModel model) {
    return (BuildContext context) => ScopedModelDescendant<SettingsModel>(
          builder: (
            BuildContext context,
            Widget child,
            SettingsModel settingsModel,
          ) =>
              LicensePage(),
        );
  }

  @override
  Widget getRow(SettingsModel model, BuildContext context) {
    return ListTile(
      leading: Icon(Icons.copyright),
      title: Text('Licenses'),
      onTap: () => Navigator.of(context).pushNamed(route),
    );
  }

  @override
  String get id => 'license';

  @override
  String get route => '/license';
}

/// Concrete implementation of [SettingEntry] for component-based settings.
class ComponentSettingEntry implements SettingEntry {
  /// The set of known setting ids with special subtitle handling.
  static const String settingIdWifi = 'wifi';
  static const String settingIdDatetime = 'datetime';
  static const String settingIdSystem = 'system';

  final String _id;
  final String _title;
  final String _component;
  final int _iconRes;

  ComponentSettingEntry(this._id, this._title, this._iconRes, this._component);

  @override
  String get id => _id;

  @override
  String get route => '/$_id';

  @override
  Future<EmbeddedModule> embedModule() async {
    return EmbeddedModule(name: _title, componentUrl: componentPath);
  }

  String get componentPath =>
      'fuchsia-pkg://fuchsia.com/$_component#meta/$_component.cmx';

  @override
  WidgetBuilder getRouteBuilder(SettingsModel model) {
    // For now, the only non component based entry is the license page.
    return (BuildContext context) =>
        _buildModule(_title, () => model.getModule(this));
  }

  @override
  Widget getRow(SettingsModel model, BuildContext context) {
    String subtitle;

    switch (id) {
      case settingIdWifi:
        subtitle = model.wifiStatus;
        break;
      case settingIdDatetime:
        subtitle = model.datetimeStatus;
        break;
      case settingIdSystem:
        subtitle = '${model.hostname} '
            '${model.networkAddresses} '
            '${model.buildInfo}';
        break;
    }

    return ListTile(
      leading: _iconRes != null
          ? Icon(IconData(_iconRes, fontFamily: 'MaterialIcons'))
          : null,
      title: Text(_title),
      subtitle: subtitle != null && subtitle.isNotEmpty ? Text(subtitle) : null,
      onTap: () => Navigator.of(context).pushNamed(route),
    );
  }

  // Returns the [Scaffold] widget for the root view of the module.
  Widget _buildModule(String title, Widget getModView()) {
    return ScopedModelDescendant<SettingsModel>(
      builder: (
        BuildContext context,
        Widget child,
        SettingsModel settingsModel,
      ) =>
          Scaffold(
        appBar: AppBar(
          title: Text(title),
        ),
        body: getModView(),
      ),
    );
  }
}
