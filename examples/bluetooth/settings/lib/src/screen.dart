// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.bluetooth.service.interfaces/control.fidl.dart'
    as bluetooth;
import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';
import 'modular/module_model.dart';

class _SettingsScaffold extends StatelessWidget {
  Widget _getDiscoveredDevicesWidget(
      BuildContext context, SettingsModuleModel moduleModel) {
    if (moduleModel.discoveredDevices.isEmpty) {
      return new Center(child: const Text('No devices found'));
    }

    return new ListView(
        children: ListTile
            .divideTiles(
                context: context,
                tiles: moduleModel.discoveredDevices
                    .map((bluetooth.RemoteDevice device) {
                  return new ListTile(
                      title: new Text(device.name ?? '(unknown)'),
                      subtitle: new Text(device.address));
                }))
            .toList());
  }

  Widget _getPopUpMenuButton(SettingsModuleModel moduleModel) {
    return new PopupMenuButton<int>(
        onSelected: (int index) => moduleModel.setActiveAdapterByIndex(index),
        itemBuilder: (BuildContext context) {
          int index = 0;
          return moduleModel.adapters
              .map((bluetooth.AdapterInfo adapter) => new PopupMenuItem<int>(
                    enabled: !moduleModel.isActiveAdapter(adapter.identifier),
                    value: index++,
                    child: new Text(adapter.address +
                        (moduleModel.isActiveAdapter(adapter.identifier)
                            ? ' (active)'
                            : '')),
                  ))
              .toList();
        });
  }

  @override
  Widget build(BuildContext context) {
    return new ScopedModelDescendant<SettingsModuleModel>(builder: (
      BuildContext context,
      Widget child,
      SettingsModuleModel moduleModel,
    ) {
      return new Scaffold(
        appBar: new AppBar(
          title: new Text(
              'Bluetooth Settings (${moduleModel.activeAdapterDescription})'),
          bottom: moduleModel.isDiscovering
              ? new PreferredSize(
                  child: new LinearProgressIndicator(),
                  preferredSize: Size.zero)
              : null,
          actions: <Widget>[
            new IconButton(
                tooltip: 'Discover Bluetooth devices',
                onPressed: (moduleModel.hasActiveAdapter &&
                        !moduleModel.isDiscoveryRequestPending)
                    ? () => moduleModel.toggleDiscovery()
                    : null,
                icon: new Icon(Icons.bluetooth_searching)),
            _getPopUpMenuButton(moduleModel),
          ],
        ),
        body: _getDiscoveredDevicesWidget(context, moduleModel),
      );
    });
  }
}

/// Root Widget of the Settings example module.
class SettingsScreen extends StatelessWidget {
  /// Constructor
  SettingsScreen({Key key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return new _SettingsScaffold();
  }
}
