// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:flutter/material.dart';

const Color _kCloseColor = const Color(0xFFEF9A9A);
const Color _kTitleBarColor = const Color(0xFF90CAF9);
const Color _kResizerColor = const Color(0xFF00E676);
const Color _kZoomerColor = const Color(0xFFE6EE9C);
const Radius _kWindowDecorationCorner = const Radius.circular(8.0);
const double _kWindowDecorationExtent = 24.0;
const double _kBaseWindowElevation = 2.0;
const double _kIncrementalWindowElevation = 20.0;
const Size _kInitialWindowSize = const Size(400.0, 200.0);

class Window {
  Window({
    this.initialRect,
    this.title,
    this.child,
  });

  final Rect initialRect;
  final String title;
  final Widget child;
}

class WindowTitleBar extends StatelessWidget {
  WindowTitleBar({
    Key key,
    this.title,
    this.onClosed,
    this.onActivate,
    this.onMoved,
  })
      : super(key: key);

  final String title;
  final VoidCallback onClosed;
  final VoidCallback onActivate;
  final GestureDragUpdateCallback onMoved;

  @override
  Widget build(BuildContext context) {
    return new Container(
      height: _kWindowDecorationExtent,
      child: new Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          new Expanded(
            child: new GestureDetector(
              onTap: onActivate,
              onPanStart: (DragStartDetails details) {
                onActivate();
              },
              onPanUpdate: onMoved,
              child: new Container(
                decoration: const BoxDecoration(
                  color: _kTitleBarColor,
                ),
                alignment: FractionalOffset.centerLeft,
                padding: const EdgeInsets.only(left: 8.0),
                child: new Text(title ?? 'Untitled',
                    style: Theme.of(context).textTheme.subhead),
              ),
            ),
          ),
          new GestureDetector(
            onTap: onClosed,
            child: new Container(
              decoration: const BoxDecoration(
                color: _kCloseColor,
              ),
              alignment: FractionalOffset.center,
              child: new Icon(Icons.close),
            ),
          ),
        ],
      ),
    );
  }
}

class WindowResizer extends StatelessWidget {
  WindowResizer({Key key, this.onResized, this.elevation}) : super(key: key);

  final GestureDragUpdateCallback onResized;
  final double elevation;

  @override
  Widget build(BuildContext context) {
    return new PhysicalModel(
      elevation: elevation,
      color: _kResizerColor,
      borderRadius: const BorderRadius.only(
        bottomRight: _kWindowDecorationCorner,
      ),
      child: new GestureDetector(
        onPanUpdate: onResized,
        child: new Container(
          width: _kWindowDecorationExtent,
          height: _kWindowDecorationExtent,
          decoration: const BoxDecoration(),
        ),
      ),
    );
  }
}

class WindowZoomer extends StatelessWidget {
  WindowZoomer({Key key, this.onZoomed, this.elevation}) : super(key: key);

  final GestureDragUpdateCallback onZoomed;
  final double elevation;

  @override
  Widget build(BuildContext context) {
    return new PhysicalModel(
      elevation: elevation,
      color: _kZoomerColor,
      borderRadius: const BorderRadius.only(
        bottomLeft: _kWindowDecorationCorner,
      ),
      child: new GestureDetector(
        onPanUpdate: onZoomed,
        child: new Container(
          width: _kWindowDecorationExtent,
          height: _kWindowDecorationExtent,
          decoration: const BoxDecoration(),
        ),
      ),
    );
  }
}

class WindowFrame extends StatefulWidget {
  WindowFrame({
    this.window,
    this.elevation,
  })
      : super(key: new ObjectKey(window));

  final Window window;
  final double elevation;

  @override
  _WindowFrameState createState() => new _WindowFrameState();
}

class _WindowFrameState extends State<WindowFrame> {
  @override
  void initState() {
    super.initState();
    if (widget.window.initialRect != null) {
      _offset = widget.window.initialRect.topLeft;
      _size = widget.window.initialRect.size;
    }
  }

  Offset _offset = Offset.zero;
  Size _size = _kInitialWindowSize;
  double _zoomX = 1.0;
  double _zoomY = 1.0;

  void _handleResizerDrag(DragUpdateDetails details) {
    setState(() {
      _size = new Size(math.max(0.0, _size.width + details.delta.dx / _zoomX),
          math.max(0.0, _size.height + details.delta.dy / _zoomY));
    });
  }

  void _handleZoomerDrag(DragUpdateDetails details) {
    setState(() {
      if (_size.width > 0.0) {
        _zoomX = (_size.width * _zoomX - details.delta.dx) / _size.width;
        _offset += new Offset(details.delta.dx, 0.0);
      }
      if (_size.height > 0.0) {
        _zoomY = (_size.height * _zoomY + details.delta.dy) / _size.height;
      }
    });
  }

  void _handleMove(DragUpdateDetails details) {
    setState(() {
      _offset += details.delta;
    });
  }

  void _handleClose() {
    WindowManager.of(context)?.removeWindow(widget.window);
  }

  void _handleActive() {
    WindowManager.of(context)?.activateWindow(widget.window);
  }

  @override
  Widget build(BuildContext context) {
    return new Positioned(
      left: _offset.dx,
      top: _offset.dy,
      width: _size.width * _zoomX + _kWindowDecorationExtent,
      height: _size.height * _zoomY + _kWindowDecorationExtent * 1.5,
      child: new Stack(
        children: <Widget>[
          new Positioned(
            right: 0.0,
            bottom: 0.0,
            child: new WindowResizer(
              onResized: _handleResizerDrag,
              elevation: widget.elevation,
            ),
          ),
          new Positioned(
            left: 0.0,
            bottom: 0.0,
            child: new WindowZoomer(
              onZoomed: _handleZoomerDrag,
              elevation: widget.elevation,
            ),
          ),
          new Positioned.fill(
            left: _kWindowDecorationExtent * 0.5,
            right: _kWindowDecorationExtent * 0.5,
            bottom: _kWindowDecorationExtent * 0.5,
            child: new PhysicalModel(
              elevation: widget.elevation,
              color: _kTitleBarColor,
              borderRadius: const BorderRadius.only(
                topLeft: _kWindowDecorationCorner,
                topRight: _kWindowDecorationCorner,
              ),
              child: new Column(
                children: <Widget>[
                  new WindowTitleBar(
                    title: widget.window.title,
                    onActivate: _handleActive,
                    onClosed: _handleClose,
                    onMoved: _handleMove,
                  ),
                  new Expanded(
                    child: new FittedBox(
                      fit: BoxFit.contain,
                      child: new SizedBox(
                        width: _size.width,
                        height: _size.height,
                        child: widget.window.child,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class WindowManager extends StatefulWidget {
  WindowManager({
    Key key,
    this.wallpaper,
    this.decorations,
  })
      : super(key: key);

  final Widget wallpaper;
  final List<Widget> decorations;

  static WindowManagerState of(BuildContext context) {
    return context.ancestorStateOfType(const TypeMatcher<WindowManagerState>());
  }

  @override
  WindowManagerState createState() => new WindowManagerState();
}

class WindowManagerState extends State<WindowManager> {
  final List<Window> _windows = <Window>[];
  final Map<Window, VoidCallback> _closeCallbacks = <Window, VoidCallback>{};

  @override
  void dispose() {
    super.dispose();
    for (VoidCallback callback in _closeCallbacks.values.toList()) {
      if (callback != null) callback();
    }
  }

  void addWindow(Window window, {VoidCallback onClose}) {
    setState(() {
      _windows.add(window);
      if (onClose != null) _closeCallbacks[window] = onClose;
    });
  }

  void removeWindow(Window window) {
    setState(() {
      _windows.remove(window);
    });
    VoidCallback callback = _closeCallbacks.remove(window);
    if (callback != null) callback();
  }

  void activateWindow(Window window) {
    setState(() {
      if (_windows.remove(window)) _windows.add(window);
    });
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> children = <Widget>[];
    if (widget.wallpaper != null) children.add(widget.wallpaper);
    children.addAll(widget.decorations);

    double elevation = _kBaseWindowElevation;
    for (Window window in _windows) {
      children.add(new WindowFrame(window: window, elevation: elevation));
      elevation += _kIncrementalWindowElevation;
    }

    return new Stack(children: children);
  }
}
