// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';

import 'modular/module_model.dart';

import 'widgets/scan_filter_button.dart';
import 'widgets/scan_results_widget.dart';

/// Root Widget of the BLE Scanner module.
class BLEScannerScreen extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return new ScopedModelDescendant<BLEScannerModuleModel>(builder: (
      BuildContext context,
      Widget child,
      BLEScannerModuleModel moduleModel,
    ) {
      return new Scaffold(
          appBar: new AppBar(
              title: new Text('BLE Scanner'),
              bottom: moduleModel.isScanning
                  ? new PreferredSize(
                      child: new LinearProgressIndicator(),
                      preferredSize: Size.zero)
                  : null,
              actions: <Widget>[
                new IconButton(
                    tooltip: 'Scan for BLE devices',
                    onPressed: moduleModel.isScanRequestPending
                        ? null
                        : () => moduleModel.toggleScan(),
                    icon: new Icon(Icons.bluetooth_searching)),
                new ScanFilterButton(),
              ]),
          body: new ScanResultsWidget());
    });
  }
}
