// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.modular.services.device/device_context.fidl.dart';
import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import 'device_shell_impl.dart';
import 'device_shell_factory_impl.dart';
import 'device_shell_factory_widget.dart';

const String _kDefaultUserName = 'user1';
const Color _kFuchsiaColor = const Color(0xFFFF00C0);

class _UserPickerScreen extends StatefulWidget {
  _UserPickerScreen({Key key}) : super(key: key);

  @override
  _UserPickerScreenState createState() => new _UserPickerScreenState();
}

class _UserPickerScreenState extends State<_UserPickerScreen> {
  DeviceContext _deviceContext;
  UserProvider _userProvider;
  List<String> _users;
  ChildViewConnection _childViewConnection;

  set deviceContext(DeviceContext deviceContext) {
    _deviceContext = deviceContext;
  }

  set userProvider(UserProvider userProvider) {
    _userProvider = userProvider;
    userProvider.previousUsers((List<String> users) {
      setState(() {
        _users = users;
      });
    });
  }

  void _loginUser(String user) {
    final InterfacePair<ViewOwner> viewOwner = new InterfacePair<ViewOwner>();
    final InterfacePair<UserController> userController =
        new InterfacePair<UserController>();
    _userProvider?.login(
      user,
      null,
      null,
      viewOwner.passRequest(),
      userController.passRequest(),
    );
    setState(() {
      _childViewConnection = new ChildViewConnection(viewOwner.passHandle());
    });
  }

  void _defaultUser() {
    _userProvider?.addUser(_kDefaultUserName, null, "ledger.fuchsia.com");
    _loginUser(_kDefaultUserName);
  }

  @override
  Widget build(BuildContext context) {
    if (_childViewConnection != null) {
      return new ChildView(connection: _childViewConnection);
    }

    final List<Widget> children = <Widget>[];
    if (_users != null) {
      if (_users.isNotEmpty) {
        // Add list of previous users.
        children.addAll(
          _users.map((String user) {
            return new Container(
              margin: const EdgeInsets.all(8.0),
              child: new RaisedButton(
                onPressed: () => _loginUser(user),
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
              onPressed: _defaultUser,
              child: new Text('Login as default user: $_kDefaultUserName'),
            ),
          ),
        );
      }
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

    return new Material(
      color: Colors.blue[200],
      child: new Container(
        child: new Stack(
          children: [
            new Center(
              child: new Column(
                mainAxisSize: MainAxisSize.min,
                children: children,
              ),
            ),
            // Add shutdown button.
            new Align(
              alignment: FractionalOffset.bottomCenter,
              child: new Container(
                margin: const EdgeInsets.all(16.0),
                child: new RaisedButton(
                  onPressed: () => _deviceContext?.shutdown(),
                  child: new Text('Shutdown'),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

void main() {
  GlobalKey<_UserPickerScreenState> userPickerKey =
      new GlobalKey<_UserPickerScreenState>();
  DeviceShellFactoryWidget deviceShellFactoryWidget =
      new DeviceShellFactoryWidget(
    applicationContext: new ApplicationContext.fromStartupInfo(),
    deviceShellFactory: new DeviceShellFactoryImpl(
      deviceShell: new DeviceShellImpl(),
      onUserProviderReceived: (UserProvider userProvider) {
        userPickerKey.currentState.userProvider = userProvider;
      },
      onDeviceContextReceived: (DeviceContext deviceContext) {
        userPickerKey.currentState.deviceContext = deviceContext;
      },
    ),
    child: new _UserPickerScreen(key: userPickerKey),
  );

  runApp(deviceShellFactoryWidget);

  deviceShellFactoryWidget.advertise();
}
