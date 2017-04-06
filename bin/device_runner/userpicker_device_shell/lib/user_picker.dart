// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.widgets/hacks.dart' as hacks;

import 'user_picker_device_shell_factory_model.dart';

const String _kDefaultUserName = 'user1';
const Color _kFuchsiaColor = const Color(0xFFFF0080);
const double _kButtonContentWidth = 180.0;
const double _kButtonContentHeight = 80.0;

typedef void OnLoginRequest(String user, UserProvider userProvider);

class UserPicker extends StatelessWidget {
  final OnLoginRequest onLoginRequest;

  UserPicker({this.onLoginRequest});

  @override
  Widget build(BuildContext context) =>
      new ScopedModelDescendant<UserPickerDeviceShellFactoryModel>(builder: (
        BuildContext context,
        Widget child,
        UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel,
      ) {
        final List<Widget> children = <Widget>[];
        if (userPickerDeviceShellFactoryModel.users != null) {
          if (userPickerDeviceShellFactoryModel.users.isNotEmpty) {
            // Add list of previous users.
            children.addAll(
              userPickerDeviceShellFactoryModel.users.map((String user) {
                return new Container(
                  margin: const EdgeInsets.all(8.0),
                  child: new RaisedButton(
                    onPressed: () => _loginUser(
                          user,
                          userPickerDeviceShellFactoryModel,
                        ),
                    child: new Text('Login as $user'),
                  ),
                );
              }),
            );
          } else {
            // Option to login as default user.
            children.add(
              new Container(
                margin: const EdgeInsets.all(8.0),
                child: new RaisedButton(
                  onPressed: () => _loginUser(
                        _kDefaultUserName,
                        userPickerDeviceShellFactoryModel,
                      ),
                  child: new Container(
                    width: _kButtonContentWidth,
                    height: _kButtonContentHeight,
                    child: new Center(
                      child: new Text(
                        'Login as default user: $_kDefaultUserName',
                      ),
                    ),
                  ),
                ),
              ),
            );
          }
          // Option to enter a username.
          children.add(
            new Container(
              decoration: new BoxDecoration(
                backgroundColor: Colors.grey[300],
                borderRadius: new BorderRadius.circular(2.0),
              ),
              margin: const EdgeInsets.all(8.0),
              padding: const EdgeInsets.symmetric(horizontal: 16.0),
              child: new Container(
                width: _kButtonContentWidth,
                height: _kButtonContentHeight,
                child: new Overlay(initialEntries: <OverlayEntry>[
                  new OverlayEntry(
                    builder: (BuildContext context) => new Center(
                          // TODO(apwilson): Use TextField ONCE WE HAVE A PROPER
                          // IME ON FUCHSIA!
                          child: new hacks.RawKeyboardTextField(
                            decoration: new InputDecoration(
                              hintText: 'Enter username',
                            ),
                            onSubmitted: (String user) => _loginUser(
                                  user,
                                  userPickerDeviceShellFactoryModel,
                                ),
                          ),
                        ),
                  ),
                ]),
              ),
            ),
          );
        } else {
          children.add(
            new Container(
              width: 64.0,
              height: 64.0,
              child: new CircularProgressIndicator(
                valueColor: new AlwaysStoppedAnimation<Color>(_kFuchsiaColor),
              ),
            ),
          );
        }

        return new Column(
          mainAxisSize: MainAxisSize.min,
          children: children,
        );
      });

  void _loginUser(
    String user,
    UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel,
  ) {
    print('UserPicker: Logging in as $user!');
    onLoginRequest?.call(user, userPickerDeviceShellFactoryModel.userProvider);
  }
}
