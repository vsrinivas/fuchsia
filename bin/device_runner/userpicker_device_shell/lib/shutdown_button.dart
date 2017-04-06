// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'user_picker_device_shell_factory_model.dart';

class ShutdownButton extends StatelessWidget {
  @override
  Widget build(BuildContext context) =>
      new ScopedModelDescendant<UserPickerDeviceShellFactoryModel>(
        builder: (
          BuildContext context,
          Widget child,
          UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel,
        ) =>
            new RaisedButton(
              onPressed: () =>
                  userPickerDeviceShellFactoryModel.deviceContext?.shutdown(),
              child: child,
            ),
        child: new Text('Shutdown'),
      );
}
