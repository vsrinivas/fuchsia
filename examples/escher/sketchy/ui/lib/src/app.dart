// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:garnet.examples.escher.common.services/escher_demo.fidl.dart';

typedef ServiceProvider AppLauncher(String msg);

/// The root application.
class App extends StatefulWidget {
  final AppLauncher _appLauncher;

  App(this._appLauncher);

  @override
  AppState createState() => new AppState(_appLauncher);
}

// Initially, displays a "Launch App" button.  When this is clicked, it:
//   - launches the "sketchy" app.
//   - rebuilds the contained Widget to capture touch/keyboard events and
//     forward them to the "sketchy" app.
class AppState extends State<App> {
  final AppLauncher _appLauncher;
  EscherDemoProxy _escherDemo;

  Offset _lastPoint;
  int _touchId = 0;

  AppState(this._appLauncher);

  @override
  Widget build(BuildContext context) {
    return new MaterialApp(
        title: 'Sketchy UI',
        theme: new ThemeData(
          primarySwatch: Colors.deepOrange,
        ),
        home: (_escherDemo == null)
            ? new Material(child: new AppLauncherWidget(launchSketchy))
            : new AppInputWidget(_escherDemo, this));
  }

  ServiceProvider launchSketchy(String appName) {
    if (_escherDemo == null) {
      ServiceProvider serviceProvider;
      setState(() {
        _escherDemo = new EscherDemoProxy();
        serviceProvider = _appLauncher(appName);
        connectToService(serviceProvider, _escherDemo.ctrl);
      });
      return serviceProvider;
    } else {
      return null;
    }
  }

  void onPanStart(DragStartDetails details) {
    assert(_lastPoint == null);
    _touchId += 1;
    _lastPoint = details.globalPosition * 2.0;
    _escherDemo.handleTouchBegin(_touchId, _lastPoint.dx, _lastPoint.dy);
  }

  void onPanUpdate(DragUpdateDetails details) {
    assert(_lastPoint != null);
    _lastPoint = details.globalPosition * 2.0;
    _escherDemo.handleTouchContinue(_touchId, _lastPoint.dx, _lastPoint.dy);
  }

  void onPanEnd(DragEndDetails details) {
    assert(_lastPoint != null);
    _escherDemo.handleTouchEnd(_touchId, _lastPoint.dx, _lastPoint.dy);
    _lastPoint = null;
  }

  void onKey(RawKeyEvent event) {
    if (event is RawKeyDownEvent) {
      RawKeyEventDataFuchsia data = event.data;

      // TODO: This works for ASCII letters, but not for ESCAPE key, etc.
      _escherDemo.handleKeyPress(data.codePoint);
    }
  }
}

// Displays a "Launch App" button for AppState.
class AppLauncherWidget extends StatelessWidget {
  final AppLauncher _appLauncher;
  AppLauncherWidget(this._appLauncher);

  @override
  Widget build(BuildContext context) {
    return new Container(
        color: Colors.grey[700],
        child: new Center(
            child: new RaisedButton(
                child: new Text('Launch App'),
                onPressed: () {
                  _appLauncher("sketchy");
                })));
  }
}

// Forwards input events to the "sketchy" app.
class AppInputWidget extends StatelessWidget {
  final EscherDemo _escherDemo;
  final AppState _appState;
  AppInputWidget(this._escherDemo, this._appState);

  @override
  Widget build(BuildContext context) {
    final focusNode = new FocusNode();
    FocusScope.of(context).requestFocus(focusNode);
    return new RawKeyboardListener(
        onKey: _appState.onKey,
        focusNode: focusNode,
        child: new GestureDetector(
            onPanStart: _appState.onPanStart,
            onPanUpdate: _appState.onPanUpdate,
            onPanEnd: _appState.onPanEnd,
            child: new Container(
              color: Colors.grey[500],
              child: new Center(child: new Text('Waiting for app to finish')),
            )));
  }
}
