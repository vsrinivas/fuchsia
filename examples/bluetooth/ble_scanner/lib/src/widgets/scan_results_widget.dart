// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:lib.bluetooth.fidl/low_energy.fidl.dart' as ble;

import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';

import '../manufacturer_names.dart';
import '../modular/module_model.dart' as model;

/// A scrollable view that displays BLE scan results
class ScanResultsWidget extends StatefulWidget {
  @override
  ScanResultsState createState() => new ScanResultsState();
}

class _AdvertisingDataEntry {
  final String fieldTitle;
  final WidgetBuilder widgetBuilder;

  _AdvertisingDataEntry(this.fieldTitle, this.widgetBuilder);
}

/// State corresponding to ScanResultsWidget
class ScanResultsState extends State<ScanResultsWidget> {
  final Map<String, bool> _expandedStateMap = <String, bool>{};

  String _connectionStateString(model.ConnectionState connState) {
    switch (connState) {
      case model.ConnectionState.notConnected:
        return 'not connected';
      case model.ConnectionState.connecting:
        return 'connecting...';
      case model.ConnectionState.connected:
        return 'connected';
    }
    return '(unknown)';
  }

  Color _connectionStateColor(model.ConnectionState connState) {
    switch (connState) {
      case model.ConnectionState.notConnected:
        return Colors.grey[400];
      case model.ConnectionState.connecting:
        return Colors.amber[400];
      case model.ConnectionState.connected:
        return Colors.green;
    }
    return Colors.black;
  }

  Widget _buildHeader(ble.RemoteDevice device) {
    return new ScopedModelDescendant<model.BLEScannerModuleModel>(builder: (
      BuildContext context,
      Widget child,
      model.BLEScannerModuleModel moduleModel,
    ) {
      var connState = moduleModel.getPeripheralState(device.identifier);
      return new Row(children: <Widget>[
        new Expanded(
            flex: 2,
            child: new Container(
                margin: const EdgeInsets.only(left: 24.0),
                child: new FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: FractionalOffset.centerLeft,
                    child:
                        new Text(device.advertisingData.name ?? '(unknown)')))),
        new Container(
            margin: const EdgeInsets.only(left: 24.0),
            child: new Text(
                device.connectable ? _connectionStateString(connState) : '',
                style: new TextStyle(
                    fontStyle: FontStyle.italic,
                    color: _connectionStateColor(connState)))),
        new Container(
            margin: const EdgeInsets.only(left: 24.0),
            child: new Text('RSSI: ${device.rssi?.value ?? 'unknown'} dBm'))
      ]);
    });
  }

  String toHexString(final List<int> data) {
    return data
        .map((int byte) => byte.toRadixString(16).padLeft(2, '0'))
        .join(' ');
  }

  Widget _buildAdvertisingDataWidget(ble.RemoteDevice device) {
    List<_AdvertisingDataEntry> entries = <_AdvertisingDataEntry>[];

    TextStyle textStyle = new TextStyle(
        color: Colors.grey[700], fontWeight: FontWeight.w500, fontSize: 12.0);

    int currentMaxTitleLength = 0;
    double textScaleFactor =
        MediaQuery.of(context, nullOk: true)?.textScaleFactor ?? 1.0;

    if (device.advertisingData.txPowerLevel != null) {
      const String title = 'Tx Power Level';
      currentMaxTitleLength = max(currentMaxTitleLength, title.length);
      entries.add(new _AdvertisingDataEntry(
          title,
          (BuildContext context) => new Text(
              '${device.advertisingData.txPowerLevel.value} dBm',
              style: textStyle)));
    }

    if (device.advertisingData.serviceUuids?.isNotEmpty ?? false) {
      const String title = 'Service UUIDs';
      currentMaxTitleLength = max(currentMaxTitleLength, title.length);
      entries.add(new _AdvertisingDataEntry(
          title,
          (BuildContext context) => new Column(
              children: device.advertisingData.serviceUuids
                  .map((String uuid) => new Text(uuid, style: textStyle))
                  .toList())));
    }

    device.advertisingData.serviceData
        ?.forEach((final String uuid, final List<int> data) {
      String title = 'Service Data ($uuid)';
      currentMaxTitleLength = max(currentMaxTitleLength, title.length);
      entries.add(new _AdvertisingDataEntry(
          title,
          (BuildContext context) =>
              new Text(toHexString(data), style: textStyle)));
    });

    device.advertisingData.manufacturerSpecificData
        ?.forEach((final int manufacturerId, final List<int> data) {
      String title =
          'Manufacturer Data (${getManufacturerName(manufacturerId)})';
      currentMaxTitleLength = max(currentMaxTitleLength, title.length);
      entries.add(new _AdvertisingDataEntry(
          title,
          (BuildContext context) =>
              new Text(toHexString(data), style: textStyle)));
    });

    device.advertisingData.uris?.forEach((var uri) {
      const String title = 'URI';
      currentMaxTitleLength = max(currentMaxTitleLength, title.length);
      entries.add(new _AdvertisingDataEntry(
          title, (BuildContext context) => new Text(uri)));
    });

    if (entries.isEmpty) {
      return new Text('No data', style: textStyle);
    }

    return new Container(
        alignment: FractionalOffset.center,
        child: new Column(
            children: entries
                .map((final _AdvertisingDataEntry entry) => new Padding(
                    padding: const EdgeInsets.only(bottom: 5.0),
                    child: new Row(children: <Widget>[
                      new Container(
                          width: currentMaxTitleLength * textScaleFactor * 8.0,
                          child: new Text('${entry.fieldTitle}:',
                              style: new TextStyle(
                                  color: Colors.grey[700],
                                  fontWeight: FontWeight.w600,
                                  fontSize: 12.0))),
                      new Container(
                          margin: const EdgeInsets.only(left: 30.0),
                          child: new Builder(builder: entry.widgetBuilder))
                    ])))
                .toList()));
  }

  Widget _buildConnectionWidget(ble.RemoteDevice device) {
    return new ScopedModelDescendant<model.BLEScannerModuleModel>(builder: (
      BuildContext context,
      Widget child,
      model.BLEScannerModuleModel moduleModel,
    ) {
      if (!device.connectable) {
        return const Text('Not connectable');
      }

      var connState = moduleModel.getPeripheralState(device.identifier);

      if (connState == model.ConnectionState.connecting) {
        return const Text('Connecting...');
      }

      if (connState == model.ConnectionState.notConnected) {
        return new FlatButton(
            onPressed: () => moduleModel.connectPeripheral(device.identifier),
            child: const Text('Connect'),
            textTheme: ButtonTextTheme.accent);
      }

      return new FlatButton(
          onPressed: () => moduleModel.disconnectPeripheral(device.identifier),
          child: const Text('Disconnect'),
          textTheme: ButtonTextTheme.accent);
    });
  }

  Widget _buildBody(ble.RemoteDevice device) {
    return new Column(children: <Widget>[
      new Container(
          margin: const EdgeInsets.only(left: 40.0, right: 24.0, bottom: 24.0),
          child: new Center(child: _buildAdvertisingDataWidget(device))),
      const Divider(height: 1.0),
      new Container(
          padding: const EdgeInsets.symmetric(vertical: 16.0),
          child: new Container(
              margin: const EdgeInsets.only(right: 8.0),
              child: _buildConnectionWidget(device)))
    ]);
  }

  @override
  Widget build(BuildContext context) {
    return new ScopedModelDescendant<model.BLEScannerModuleModel>(builder: (
      BuildContext context,
      Widget child,
      model.BLEScannerModuleModel moduleModel,
    ) {
      if (moduleModel.discoveredDevices.isEmpty) {
        return new Center(child: const Text('No devices found'));
      }

      return new SingleChildScrollView(
          child: new ExpansionPanelList(
              expansionCallback: (int index, bool isExpanded) {
                setState(() {
                  ble.RemoteDevice device =
                      moduleModel.discoveredDevices.elementAt(index);
                  _expandedStateMap[device.identifier] = !isExpanded;
                });
              },
              children:
                  moduleModel.discoveredDevices.map((ble.RemoteDevice device) {
                return new ExpansionPanel(
                    headerBuilder: (_, __) => _buildHeader(device),
                    body: _buildBody(device),
                    isExpanded: _expandedStateMap[device.identifier] ?? false);
              }).toList()));
    });
  }
}
