// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'shutdown_button.dart';
import 'user_picker.dart';

class UserPickerScreen extends StatelessWidget {
  final OnLoginRequest onLoginRequest;

  UserPickerScreen({this.onLoginRequest});

  @override
  Widget build(BuildContext context) => new Material(
        color: Colors.grey[900],
        child: new Container(
          child: new Stack(
            children: <Widget>[
              /// Add Fuchsia logo.
              new Align(
                alignment: FractionalOffset.bottomRight,
                child: new Container(
                  margin: const EdgeInsets.only(
                    right: 16.0,
                    bottom: 8.0,
                  ),
                  child: new Image.asset(
                    'packages/userpicker_device_shell/res/fuchsia.png',
                    width: 256.0,
                    height: 256.0,
                  ),
                ),
              ),
              new Center(child: new UserPicker(onLoginRequest: onLoginRequest)),
              // Add shutdown button.
              new Align(
                alignment: FractionalOffset.bottomCenter,
                child: new Container(
                  margin: const EdgeInsets.all(16.0),
                  child: new ShutdownButton(),
                ),
              ),
            ],
          ),
        ),
      );
}
