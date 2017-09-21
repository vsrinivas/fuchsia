// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';

import '../modular/module_model.dart';
import 'scan_filter_dialog.dart';

/// Button that brings up the scan filter dialog
class ScanFilterButton extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    final ThemeData theme = Theme.of(context);

    return new ScopedModelDescendant<BLEScannerModuleModel>(builder: (
      BuildContext context,
      Widget child,
      BLEScannerModuleModel moduleModel,
    ) {
      return new FlatButton(
          child: new Text('Add Filter',
              style: theme.textTheme.body1.copyWith(color: Colors.white)),
          onPressed: () {
            Navigator.push(
                context,
                new MaterialPageRoute<DismissDialogAction>(
                    builder: (BuildContext context) =>
                        new ScanFilterDialog(moduleModel: moduleModel),
                    fullscreenDialog: true));
          });
    });
  }
}
