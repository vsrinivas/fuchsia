// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.modular.services.device/device_context.fidl.dart';
import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'device_shell_impl.dart';
import 'device_shell_factory_impl.dart';
import 'device_shell_factory_widget.dart';
import 'fuchsia_compatible_input_field.dart';
import 'user_watcher_impl.dart';

const String _kDefaultUserName = 'user1';
const Color _kFuchsiaColor = const Color(0xFFFF00C0);
const double _kButtonContentWidth = 180.0;
const double _kButtonContentHeight = 80.0;

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

class _UserPickerScreen extends StatefulWidget {
  _UserPickerScreen({Key key}) : super(key: key);

  @override
  _UserPickerScreenState createState() => new _UserPickerScreenState();
}

class _UserPickerScreenState extends State<_UserPickerScreen>
    with TickerProviderStateMixin {
  UserControllerProxy _userControllerProxy;
  UserWatcherImpl _userWatcherImpl;

  DeviceContext _deviceContext;
  UserProvider _userProvider;
  List<String> _users;
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
        _users = null;
        _loadUsers();
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

  set deviceContext(DeviceContext deviceContext) {
    _deviceContext = deviceContext;
  }

  set userProvider(UserProvider userProvider) {
    _userProvider = userProvider;
    _loadUsers();
  }

  void _loadUsers() {
    _userProvider.previousUsers((List<String> users) {
      setState(() {
        _users = users;
      });
    });
  }

  void _loginUser(String user) {
    print('UserPickerDeviceShell: Logging in as $user!');

    // Add the user first just in case.
    _userProvider?.addUser(user, null, 'ledger.fuchsia.com');

    final InterfacePair<ViewOwner> viewOwner = new InterfacePair<ViewOwner>();
    _userProvider?.login(
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

  @override
  Widget build(BuildContext context) => new AnimatedBuilder(
      animation: _transitionAnimation,
      builder: (BuildContext context, Widget child) {
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
                  onPressed: () => _loginUser(_kDefaultUserName),
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
                          child: new RawKeyboardTextField(
                            decoration: new InputDecoration(
                              hintText: 'Enter username',
                            ),
                            onSubmitted: _loginUser,
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
        List<Widget> stackChildren = <Widget>[
          new Positioned.fill(
            child: new Opacity(
              opacity: 1.0 - _curvedTransitionAnimation.value,
              child: new Material(
                color: Colors.grey[900],
                child: new Container(
                  child: new Stack(
                    children: [
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
              ),
            ),
          )
        ];

        if (_childViewConnection != null) {
          stackChildren.insert(
            0,
            new Positioned.fill(
              child: new ChildView(connection: _childViewConnection),
            ),
          );
        }
        return new Stack(children: stackChildren);
      });
}
