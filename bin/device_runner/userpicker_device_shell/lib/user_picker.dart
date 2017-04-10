// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.widgets/hacks.dart' as hacks;

import 'user_picker_device_shell_factory_model.dart';

const String _kDefaultUserName = 'user1';
const String _kDefaultDeviceName = 'fuchsia';
const String _kDefaultServerName = 'ledger.fuchsia.com';
const Color _kFuchsiaColor = const Color(0xFFFF0080);
const double _kButtonContentWidth = 180.0;
const double _kButtonContentHeight = 80.0;

typedef void OnLoginRequest(String user, UserProvider userProvider);

class UserPicker extends StatelessWidget {
  final OnLoginRequest onLoginRequest;
  final TextEditingController userNameController;
  final TextEditingController deviceNameController;
  final TextEditingController serverNameController;

  UserPicker({
    this.onLoginRequest,
    this.userNameController,
    this.deviceNameController,
    this.serverNameController,
  });

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
                    child: new Text('Log in as $user'),
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
                  onPressed: () => _createAndLoginUser(
                        _kDefaultUserName,
                        _kDefaultDeviceName,
                        _kDefaultServerName,
                        userPickerDeviceShellFactoryModel,
                      ),
                  child: new Container(
                    width: _kButtonContentWidth,
                    height: _kButtonContentHeight,
                    child: new Center(
                      child: new Text(
                        'Log in as default user: $_kDefaultUserName',
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
                child: new Overlay(initialEntries: <OverlayEntry>[
                  new OverlayEntry(
                    builder: (BuildContext context) => new Center(
                          // TODO(apwilson): Use TextField ONCE WE HAVE A PROPER
                          // IME ON FUCHSIA!
                          child: new Column(
                            mainAxisSize: MainAxisSize.min,
                            children: <Widget>[
                              new hacks.RawKeyboardTextField(
                                decoration: new InputDecoration(
                                  hintText: 'Enter user name',
                                ),
                                controller: userNameController,
                              ),
                              new hacks.RawKeyboardTextField(
                                decoration: new InputDecoration(
                                  hintText: 'Enter device name',
                                ),
                                controller: deviceNameController,
                              ),
                              new hacks.RawKeyboardTextField(
                                decoration: new InputDecoration(
                                  hintText: 'Enter server name',
                                ),
                                controller: serverNameController,
                              ),
                              new Container(
                                margin: const EdgeInsets.symmetric(
                                  vertical: 16.0,
                                ),
                                child: new RaisedButton(
                                  onPressed: () => _createAndLoginUser(
                                        userNameController.text,
                                        deviceNameController.text,
                                        serverNameController.text,
                                        userPickerDeviceShellFactoryModel,
                                      ),
                                  child: new Container(
                                    width: _kButtonContentWidth - 32.0,
                                    height: _kButtonContentHeight,
                                    child: new Center(
                                      child: new Text(
                                        'Create and Log in',
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                            ],
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

  void _createAndLoginUser(
    String user,
    String deviceName,
    String serverName,
    UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel,
  ) {
    // Add the user if it doesn't already exist.
    if (!(userPickerDeviceShellFactoryModel.users?.contains(user) ?? false)) {
      if (user?.isEmpty ?? true) {
        print('Not creating user: User name needs to be set!');
        return;
      }
      if (deviceName?.isEmpty ?? true) {
        print('Not creating user: Device name needs to be set!');
        return;
      }
      if (serverName?.isEmpty ?? true) {
        print('Not creating user: Server name needs to be set!');
        return;
      }
      print(
          'UserPicker: Creating user $user with device $deviceName and server $serverName!');
      userPickerDeviceShellFactoryModel.userProvider?.addUser(
        user,
        null,
        deviceName,
        serverName,
      );
    }

    _loginUser(
      user,
      userPickerDeviceShellFactoryModel,
    );
  }

  void _loginUser(
    String user,
    UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel,
  ) {
    print('UserPicker: Logging in as $user!');
    onLoginRequest?.call(user, userPickerDeviceShellFactoryModel.userProvider);
  }
}
