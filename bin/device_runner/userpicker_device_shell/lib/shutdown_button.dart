// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'device_shell_factory_model.dart';

class ShutdownButton extends StatelessWidget {
  @override
  Widget build(BuildContext context) =>
      new ScopedModelDescendant<DeviceShellFactoryModel>(
        builder: (
          BuildContext context,
          Widget child,
          DeviceShellFactoryModel deviceShellFactoryModel,
        ) =>
            new RaisedButton(
              onPressed: () =>
                  deviceShellFactoryModel.deviceContext?.shutdown(),
              child: child,
            ),
        child: new Text('Shutdown'),
      );
}
