// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:lib.widgets/modular.dart';

import 'user_picker_device_shell_factory_model.dart';
import 'user_picker_screen.dart';
import 'user_watcher_impl.dart';

void main() {
  UserPickerDeviceShellFactoryModel userPickerDeviceShellFactoryModel =
      new UserPickerDeviceShellFactoryModel();

  DeviceShellFactoryWidget<UserPickerDeviceShellFactoryModel>
      deviceShellFactoryWidget =
      new DeviceShellFactoryWidget<UserPickerDeviceShellFactoryModel>(
    deviceShellFactoryModel: userPickerDeviceShellFactoryModel,
    child: new _ScreenManager(
      onLogout: userPickerDeviceShellFactoryModel.onLogout,
    ),
  );

  runApp(deviceShellFactoryWidget);

  deviceShellFactoryWidget.advertise();
}

class _ScreenManager extends StatefulWidget {
  VoidCallback onLogout;

  _ScreenManager({this.onLogout});

  @override
  _ScreenManagerState createState() => new _ScreenManagerState();
}

class _ScreenManagerState extends State<_ScreenManager>
    with TickerProviderStateMixin {
  UserControllerProxy _userControllerProxy;
  UserWatcherImpl _userWatcherImpl;

  ChildViewConnection _childViewConnection;

  AnimationController _transitionAnimation;
  CurvedAnimation _curvedTransitionAnimation;

  @override
  void initState() {
    super.initState();
    _userControllerProxy = new UserControllerProxy();
    _userWatcherImpl = new UserWatcherImpl(onUserLogout: () {
      print('UserPickerDeviceShell: User logged out!');
      setState(() {
        config.onLogout?.call();
        _transitionAnimation.reverse();
        // TODO(apwilson): Should need to remove the child view connection but
        // it causes a mozart deadlock in the compositor if you don't.
        _childViewConnection = null;
      });
    });
    _transitionAnimation = new AnimationController(
      value: 0.0,
      duration: const Duration(seconds: 1),
      vsync: this,
    );
    _curvedTransitionAnimation = new CurvedAnimation(
      parent: _transitionAnimation,
      curve: Curves.fastOutSlowIn,
      reverseCurve: Curves.fastOutSlowIn,
    );
  }

  @override
  void dispose() {
    super.dispose();
    _userWatcherImpl.close();
    _userControllerProxy.ctrl.close();
  }

  @override
  Widget build(BuildContext context) => new AnimatedBuilder(
        animation: _transitionAnimation,
        builder: (BuildContext context, Widget child) =>
            _childViewConnection == null
                ? child
                : new Stack(children: <Widget>[
                    new ChildView(connection: _childViewConnection),
                    new Opacity(
                      opacity: 1.0 - _curvedTransitionAnimation.value,
                      child: child,
                    ),
                  ]),
        child: new UserPickerScreen(onLoginRequest: _login),
      );

  void _login(String user, UserProvider userProvider) {
    // Add the user first just in case.
    userProvider?.addUser(user, null, 'ledger.fuchsia.com');

    final InterfacePair<ViewOwner> viewOwner = new InterfacePair<ViewOwner>();
    userProvider?.login(
      user,
      null,
      null,
      viewOwner.passRequest(),
      _userControllerProxy.ctrl.request(),
    );
    _userControllerProxy.watch(_userWatcherImpl.getHandle());

    setState(() {
      _childViewConnection = new ChildViewConnection(
        viewOwner.passHandle(),
        onAvailable: (ChildViewConnection connection) {
          print('UserPickerDeviceShell: Child view connection available!');
          _transitionAnimation.forward();
        },
        onUnavailable: (ChildViewConnection connection) {
          print('UserPickerDeviceShell: Child view connection unavailable!');
          _transitionAnimation.reverse();
        },
      );
    });
  }
}
