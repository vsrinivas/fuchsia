// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_bluetooth_control/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.settings/widgets.dart';
import 'package:lib.widgets/model.dart';

import 'bluetooth_model.dart';

/// Widget that displays bluetooth information, and allows users to
/// connect and disconnect from devices.
class BluetoothSettings extends StatelessWidget {
  const BluetoothSettings();

  @override
  Widget build(BuildContext context) =>
      ScopedModelDescendant<BluetoothSettingsModel>(
          builder: (
        BuildContext context,
        Widget child,
        BluetoothSettingsModel model,
      ) =>
              LayoutBuilder(
                  builder: (BuildContext context, BoxConstraints constraints) =>
                      Material(
                          child: _buildBluetoothSettings(
                              model: model,
                              scale:
                                  constraints.maxHeight > 360.0 ? 1.0 : 0.5))));
}

typedef BluetoothSettingsSectionBuilder = SettingsSection Function(
    BluetoothSettingsModel model, double scale);

Widget _buildBluetoothSettings(
    {@required BluetoothSettingsModel model, @required double scale}) {
  if (model.activeAdapter == null) {
    return SettingsPage(
      scale: scale,
      sections: [
        SettingsSection.error(
          scale: scale,
          description: 'No bluetooth adapters were found',
        )
      ],
    );
  }

  final page = SettingsPage(
    scale: scale,
    sections: [_connectedDevices, _availableDevices, _adapters, _settings]
        .map((BluetoothSettingsSectionBuilder sectionBuilder) =>
            sectionBuilder(model, scale))
        .toList(),
  );

  return model.pairingStatus != null
      ? Stack(children: [page, _buildPairingPopup(model, scale)])
      : page;
}

String _buildPairingMessage(PairingStatus status) {
  String deviceName = status.device.name ?? status.device.address;
  switch (status.pairingMethod) {
    case PairingMethod.consent:
      return 'Pair with "$deviceName"?';
    case PairingMethod.passkeyDisplay:
      return 'Enter the passkey below on device "$deviceName":';
    case PairingMethod.passkeyComparison:
      return 'Confirm passkey is shown on device "$deviceName":';
    case PairingMethod.passkeyEntry:
      return 'Enter passkey shown by device "$deviceName":';
  }
  return null;
}

Widget _buildPairingAction(BluetoothSettingsModel model, double scale) {
  Widget buttons = Row(
    children: <Widget>[
      RaisedButton(
        onPressed: () => model.acceptPairing(null),
        child: Text('Accept', style: _textStyle(scale)),
      ),
      Padding(padding: EdgeInsets.only(right: 48.0 * scale)),
      RaisedButton(
        onPressed: model.rejectPairing,
        child: Text('Reject', style: _textStyle(scale)),
      ),
    ],
  );

  PairingStatus status = model.pairingStatus;
  PairingMethod method = status.pairingMethod;

  if (method == PairingMethod.passkeyDisplay ||
      method == PairingMethod.passkeyComparison) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: <Widget>[
        Text(status.displayedPasskey, style: _titleTextStyle(scale)),
        Padding(padding: EdgeInsets.only(top: 16.0 * scale)),
        Container(child: buttons),
      ],
    );
  }

  if (method == PairingMethod.passkeyEntry) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: <Widget>[
        TextField(
          keyboardType: TextInputType.number,
          autofocus: true,
          onSubmitted: (input) => model.acceptPairing(input.trim()),
        ),
        Padding(padding: EdgeInsets.only(top: 16.0 * scale)),
        Container(child: buttons),
      ],
    );
  }

  return buttons;
}

Widget _buildPairingPopup(BluetoothSettingsModel model, double scale) {
  return SettingsPopup(
      onDismiss: model.rejectPairing,
      child: Material(
          color: Colors.white,
          child: FractionallySizedBox(
            widthFactor: 0.8,
            heightFactor: 0.9,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Padding(padding: EdgeInsets.only(top: 16.0 * scale)),
                Text(
                  _buildPairingMessage(model.pairingStatus),
                  style: _titleTextStyle(scale),
                ),
                ConstrainedBox(
                  constraints: BoxConstraints(maxWidth: 400.0 * scale),
                  child: Container(
                    padding: EdgeInsets.only(top: 32.0 * scale),
                    child: _buildPairingAction(model, scale),
                  ),
                ),
              ],
            ),
          )));
}

SettingsSection _settings(BluetoothSettingsModel model, double scale) {
  final discoverableSetting = SettingsSwitchTile(
    scale: scale,
    state: model.discoverable,
    text: 'Discoverable',
    onSwitch: (value) => model.setDiscoverable(discoverable: value),
  );
  final debugModeSetting = SettingsSwitchTile(
    scale: scale,
    state: model.debugMode,
    text: 'Debug Mode',
    onSwitch: (value) => model.debugMode = value,
  );
  return SettingsSection(
      title: 'Settings',
      scale: scale,
      child: SettingsItemList(
        items: [discoverableSetting, debugModeSetting],
      ));
}

SettingsSection _connectedDevices(BluetoothSettingsModel model, double scale) {
  if (model.knownDevices.isEmpty) {
    return SettingsSection.empty();
  }
  return SettingsSection(
      title: 'Known devices',
      scale: scale,
      child: SettingsItemList(
          items: model.knownDevices.map((device) => _deviceTile(
              model, device, scale, () => model.disconnect(device)))));
}

SettingsSection _availableDevices(BluetoothSettingsModel model, double scale) {
  if (model.availableDevices.isEmpty) {
    return SettingsSection.error(
      scale: scale,
      title: _availableDevicesTitle,
      description: 'No bluetooth devices available to connect',
    );
  }

  return SettingsSection(
      title: _availableDevicesTitle,
      scale: scale,
      child: SettingsItemList(
        items: model.availableDevices.map((device) =>
            _deviceTile(model, device, scale, () => model.connect(device))),
      ));
}

/// Section containing the list of adapters, both active and not.
///
/// In future, this should probably be moved somewhere more hidden, as in the
/// vast majority of cases, thre should be either one or no adapters.
SettingsSection _adapters(BluetoothSettingsModel model, double scale) {
  final _adapters = [_activeAdapterTile(model.activeAdapter, scale)]..addAll(
      model.inactiveAdapters.map((adapter) => _adapterTile(adapter, scale)));

  return SettingsSection(
    title: 'Adapters',
    scale: scale,
    child: SettingsItemList(items: _adapters),
  );
}

SettingsTile _deviceTile(BluetoothSettingsModel model, RemoteDevice device,
    double scale, VoidCallback onTap) {
  String description;
  if (model.debugMode) {
    description =
        'id: ${device.identifier}\naddress: ${device.address}\nconnected: ${device.connected}\nbonded: ${device.bonded}';
  } else {
    description = device.connected
        ? 'connected'
        : (device.bonded ? 'not connected' : null);
  }

  return SettingsTile(
    text: device.name ?? device.address,
    description: description,
    onTap: onTap,
    iconData: _icon(device),
    scale: scale,
  );
}

IconData _icon(RemoteDevice device) {
  if (device.appearance == Appearance.hidKeyboard) {
    return Icons.keyboard;
  }
  return device.connected ? Icons.bluetooth_connected : Icons.bluetooth;
}

SettingsTile _adapterTile(AdapterInfo adapter, double scale) {
  return SettingsTile(
    text: '${adapter.address} (id: ${adapter.identifier})',
    iconData: Icons.bluetooth_disabled,
    scale: scale,
  );
}

SettingsTile _activeAdapterTile(AdapterInfo adapter, double scale) {
  return SettingsTile(
    text: '${adapter.address} (id: ${adapter.identifier})',
    description: 'enabled',
    iconData: Icons.bluetooth_searching,
    scale: scale,
  );
}

const String _availableDevicesTitle = 'Available Devices';

TextStyle _titleTextStyle(double scale) => TextStyle(
      color: Colors.grey[900],
      fontSize: 48.0 * scale,
      fontWeight: FontWeight.w200,
    );

TextStyle _textStyle(double scale) => TextStyle(
      color: Colors.grey[900],
      fontSize: 36.0 * scale,
      fontWeight: FontWeight.w200,
    );
