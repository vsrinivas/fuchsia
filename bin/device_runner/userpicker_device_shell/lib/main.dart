// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.device/device_context.fidl.dart';
import 'package:apps.modular.services.device/device_shell.fidl.dart';
import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

void _log(String msg) {
  print('[UserPicker Device Shell] $msg');
}

class _DeviceShell extends DeviceShell {
  final DeviceShellBinding _binding = new DeviceShellBinding();

  void bind(InterfaceRequest<DeviceShell> request) {
    _binding.bind(this, request);
  }

  @override
  void terminate(void callback()) {
    callback();
  }
}

class _AppState {
  final DeviceContextProxy _deviceContextProxy;
  final UserProviderProxy _userProviderProxy;
  final List<String> _users;

  _AppState(this._deviceContextProxy, this._userProviderProxy, this._users);

  // API below is exposed to the view state.
  List<String> get users => _users;
  void shutdown() => _deviceContextProxy.shutdown();
  UserProviderProxy get userProviderProxy => _userProviderProxy;
}

class _DeviceShellFactory extends DeviceShellFactory {
  final _UserPickerScreenState _state;
  final DeviceShellFactoryBinding _binding = new DeviceShellFactoryBinding();

  final DeviceContextProxy _deviceContextProxy = new DeviceContextProxy();
  final UserProviderProxy _userProviderProxy = new UserProviderProxy();

  _DeviceShell _shell = new _DeviceShell();

  _DeviceShellFactory(this._state);

  void bind(InterfaceRequest<DeviceShellFactory> request) {
    _binding.bind(this, request);
  }

  // NOTE: Multiple calls to create() is broken. This is intentional as the
  // device_runner only calls create() once.
  @override
  void create(
      InterfaceHandle<DeviceContext> deviceContextHandle,
      InterfaceHandle<UserProvider> userProviderHandle,
      InterfaceRequest<DeviceShell> deviceShellRequest) {
    _log("_DeviceShellFactory.create()");
    _deviceContextProxy.ctrl.bind(deviceContextHandle);
    _userProviderProxy.ctrl.bind(userProviderHandle);
    _userProviderProxy.previousUsers((List<String> users) {
      final _AppState appState = new _AppState(_deviceContextProxy,
          _userProviderProxy, users);
      _state.setAppState(appState);
    });
    _shell.bind(deviceShellRequest);
  }
}

class _UserPickerScreen extends StatefulWidget {
  final ApplicationContext _context;
  final _UserPickerScreenState _state;

  _UserPickerScreen({Key key, ApplicationContext context})
      : super(key: key),
        _context = context,
        _state = new _UserPickerScreenState() {
    _log("UserPickerScreen()");
    final deviceShellFactory = new _DeviceShellFactory(_state);
    _context.outgoingServices.addServiceForName(
        (InterfaceRequest<DeviceShellFactory> request) {
      _log("Service request for DeviceShellFactory");
      deviceShellFactory.bind(request);
    }, DeviceShellFactory.serviceName);
  }

  @override
  _UserPickerScreenState createState() {
    return _state;
  }
}

class _UserPickerScreenState extends State<_UserPickerScreen> {
  _AppState _appState;
  bool _appStateReady = false;

  ChildViewConnection _childViewConnection;
  bool _childViewConnectionReady = false;

  _UserPickerScreenState();

  void setAppState(final _AppState appState) {
    _appState = appState;
    setState(() { _appStateReady = true; });
  }

  void _runUser(String user) {
    final InterfacePair<ViewOwner> viewOwner = new InterfacePair<ViewOwner>();
    final InterfacePair<UserController> userController = new
      InterfacePair<UserController>();
    _appState.userProviderProxy.login(user, null, null, viewOwner.passRequest(),
        userController.passRequest());
    _childViewConnection = new ChildViewConnection(viewOwner.passHandle());
    setState(() { _childViewConnectionReady = true; });

  }

  void _defaultUser() {
    _appState.userProviderProxy.addUser("user1", null, "ledger.fuchsia.com");
    _runUser("user1");
  }

  @override
  Widget build(BuildContext context) {
    if (_childViewConnectionReady) {
      return new ChildView(connection: _childViewConnection);
    }

    final List<Widget> children = <Widget>[];
    if (_appStateReady) {
      // Add shutdown button.
      children.add(
        new RaisedButton(
            onPressed: _appState.shutdown,
            child: new Text('Shutdown'),
        ),
        );

      // Add list of previous users.
      children.addAll(_appState.users.map((String user) {
        return new RaisedButton(
            onPressed: () => _runUser(user),
            child: new Text('Run as $user'),
        );
      }));

      // Option to login as default user.
      children.add(
          new RaisedButton(
            onPressed: _defaultUser,
            child: new Text('Run as default user'),
          )
      );
    }

    return new Material(
        color: Colors.blue[200],
        child: new Container(
          child: new Column(children: children),
        ),
    );
  }
}

void main() {
  runApp(new _UserPickerScreen(
        context: new ApplicationContext.fromStartupInfo()));
}
