// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/application_controller.fidl.dart';
import 'package:apps.modular.services.application/application_launcher.fidl.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.presentation/presenter.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:flutter/material.dart';

final ApplicationContext _context = new ApplicationContext.fromStartupInfo();

class LauncherData {
  const LauncherData({ this.url, this.title });
  final String url;
  final String title;
}

final List<LauncherData> _kLauncherData = <LauncherData>[
  const LauncherData(
    url: 'file:///system/apps/noodles_view',
    title: 'Noodles'
  ),
  const LauncherData(
    url: 'file:///system/apps/shapes_view',
    title: 'Shapes'
  ),
  new LauncherData(
    url: 'file:///system/apps/hello_flutter',
    title: 'Hello Flutter'
  ),
];

class ChildApplication {
  ChildApplication({ this.controller, this.connection });

  final ApplicationControllerProxy controller;
  final ChildViewConnection connection;
}

const Size _kInitialWindowSize = const Size(200.0, 200.0);
const double _kWindowPadding = 24.0;

enum WindowSide {
  topCenter,
  topRight,
  bottomRight,
}

class WindowDecoration extends StatelessWidget {
  WindowDecoration({
    Key key,
    this.side,
    this.color,
    this.onTap,
    this.onPanUpdate
  }) : super(key: key);

  final WindowSide side;
  final Color color;
  final GestureTapCallback onTap;
  final GestureDragUpdateCallback onPanUpdate;

  @override
  Widget build(BuildContext context) {
    double top, right, bottom, left, width, height;

    height = _kWindowPadding;

    if (side == WindowSide.topCenter || side == WindowSide.topRight)
      top = 0.0;

    if (side == WindowSide.topRight || side == WindowSide.bottomRight) {
      right = 0.0;
      width = _kWindowPadding;
    }

    if (side == WindowSide.topCenter) {
      left = _kWindowPadding;
      right = _kWindowPadding;
    }

    if (side == WindowSide.bottomRight)
      bottom = 0.0;

    return new Positioned(
      top: top,
      right: right,
      bottom: bottom,
      left: left,
      width: width,
      height: height,
      child: new GestureDetector(
        onTap: onTap,
        onPanUpdate: onPanUpdate,
        child: new Container(
          decoration: new BoxDecoration(
            backgroundColor: color
          )
        )
      )
    );
  }
}

class WindowTitleBar extends StatelessWidget {
  WindowTitleBar({
    Key key,
    this.onClose,
    this.onMove,
  }) : super(key: key);

  final VoidCallback onClose;
  final GestureDragUpdateCallback onMove;

  Widget build(BuildContext context) {
    return new Row(
      children: <Widget>[
        new Flexible(
          child: new GestureDetector(
            onPanUpdate: onMove,
            child: new Container(
              height: _kWindowPadding,
              decoration: new BoxDecoration(
                backgroundColor: Colors.blue[200],
              ),
            ),
          ),
        ),
        new GestureDetector(
          onTap: onClose,
          child: new Container(
            width: _kWindowPadding,
            height: _kWindowPadding,
            decoration: new BoxDecoration(
              backgroundColor: Colors.red[200],
            ),
            alignment: FractionalOffset.center,
            child: new Icon(Icons.close),
          ),
        ),
      ]
    );
  }
}

class Window extends StatefulWidget {
  Window({ Key key, this.child, this.onClose }) : super(key: key);

  final ChildApplication child;
  final ValueChanged<ChildApplication> onClose;

  @override
  _WindowState createState() => new _WindowState();
}

class _WindowState extends State<Window> {
  Offset _offset = Offset.zero;
  Size _size = _kInitialWindowSize;

  void _handleResizerDrag(DragUpdateDetails details) {
    setState(() {
      _size = new Size(
        math.max(0.0, _size.width + details.delta.dx),
        math.max(0.0, _size.height + details.delta.dy)
      );
    });
  }

  void _handleMove(DragUpdateDetails details) {
    setState(() {
      _offset += details.delta;
    });
  }

  void _handleClose() {
    config.onClose(config.child);
  }

  @override
  Widget build(BuildContext context) {
    return new Positioned(
      left: _offset.dx,
      top: _offset.dy,
      width: _size.width + _kWindowPadding * 0.5,
      height: _size.height + _kWindowPadding * 1.5,
      child: new Stack(
        children: <Widget>[
          new Positioned(
            bottom: 0.0,
            right: 0.0,
            width: _kWindowPadding,
            height: _kWindowPadding,
            child: new GestureDetector(
              onPanUpdate: _handleResizerDrag,
              child: new Container(
                decoration: new BoxDecoration(
                  backgroundColor: Colors.blue[200],
                ),
              )
            )
          ),
          new Positioned.fill(
            right: _kWindowPadding,
            bottom: _kWindowPadding,
            child: new Material(
              elevation: 8,
              child: new Column(
                children: <Widget>[
                  new WindowTitleBar(
                    onClose: _handleClose,
                    onMove: _handleMove,
                  ),
                  new Flexible(
                    child: new ChildView(connection: config.child.connection),
                  ),
                ]
              )
            )
          )
        ]
      )
    );
  }
}

class LauncherItem extends StatelessWidget {
  LauncherItem({
    Key key,
    this.url,
    this.child,
    this.onLaunch
  }) : super(key: key);

  final String url;
  final Widget child;
  final ValueChanged<ChildApplication> onLaunch;

  void _handlePressed() {
    final ServiceProviderProxy services = new ServiceProviderProxy();
    final ApplicationControllerProxy controller = new ApplicationControllerProxy();
    final ApplicationLaunchInfo launchInfo = new ApplicationLaunchInfo()
      ..url = url
      ..services = services.ctrl.request();
    _context.launcher.createApplication(launchInfo, controller.ctrl.request());
    onLaunch(new ChildApplication(
      controller: controller,
      connection: new ChildViewConnection.connect(services),
    ));
  }

  @override
  Widget build(BuildContext context) {
    return new RaisedButton(
      onPressed: _handlePressed,
      child: child
    );
  }
}

class Launcher extends StatelessWidget {
  Launcher({ Key key, this.items }) : super(key: key);

  final List<Widget> items;

  @override
  Widget build(BuildContext context) {
    return new ButtonBar(
      alignment: MainAxisAlignment.center,
      children: items
    );
  }
}

class WindowManager extends StatefulWidget {
  WindowManager({ Key key }) : super(key: key);

  @override
  _WindowManagerState createState() => new _WindowManagerState();
}

class _WindowManagerState extends State<WindowManager> {
  List<ChildApplication> _windows = <ChildApplication>[];

  void _handleLaunch(ChildApplication child) {
    setState(() {
      _windows.add(child);
    });
  }

  void _handleClose(ChildApplication child) {
    setState(() {
      _windows.remove(child);
      child.controller?.kill(null);
    });
  }

  @override
  Widget build(BuildContext context) {
    return new Material(
      child: new Stack(
        children: <Widget>[
          new Positioned(
            left: 0.0,
            right: 0.0,
            bottom: 0.0,
            child: new Launcher(items: _kLauncherData.map((LauncherData data) {
              return new LauncherItem(
                url: data.url,
                onLaunch: _handleLaunch,
                child: new Text(data.title)
              );
            }).toList())
          )
        ]..addAll(_windows.map((ChildApplication child) {
          return new Window(
            key: new ObjectKey(child),
            onClose: _handleClose,
            child: child
          );
        }))
      )
    );
  }
}

class PresenterImpl extends Presenter {
  PresenterImpl(this._key);

  final GlobalKey<_WindowManagerState> _key;
  final PresenterBinding _binding = new PresenterBinding();

  void bind(InterfaceRequest<Presenter> request) {
    _binding.bind(this, request);
  }

  @override
  void present(InterfaceHandle<ViewOwner> viewOwner) {
    _key.currentState._handleLaunch(
      new ChildApplication(connection: new ChildViewConnection(viewOwner))
    );
  }
}

void main() {
  GlobalKey<_WindowManagerState> windowManagerKey = new GlobalKey<_WindowManagerState>();

  _context.outgoingServices.addServiceForName((request) {
    new PresenterImpl(windowManagerKey).bind(request);
  }, Presenter.serviceName);

  runApp(new MaterialApp(
    title: 'Basic Window Manager',
    home: new WindowManager(key: windowManagerKey)
  ));
}
