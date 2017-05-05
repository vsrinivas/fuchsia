// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:flutter/material.dart';

const Color _kCloseColor = const Color(0xAFEF9A9A);
const Color _kTitleBarColor = const Color(0xAF90CAF9);
const Radius _kWindowDecorationCorner = const Radius.circular(8.0);
const double _kWindowDecorationExtent = 24.0;
const Size _kInitialWindowSize = const Size(400.0, 200.0);

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
                  borderRadius: const BorderRadius.only(
                    topLeft: _kWindowDecorationCorner,
                  ),
                  color: _kTitleBarColor,
                ),
                alignment: FractionalOffset.centerLeft,
                padding: const EdgeInsets.only(left: 8.0),
                child: new Text(title ?? 'Untitled', style: Theme.of(context).textTheme.subhead),
              ),
            ),
          ),
          new GestureDetector(
            onTap: onClosed,
            child: new Container(
              decoration: const BoxDecoration(
                borderRadius: const BorderRadius.only(
                  topRight: _kWindowDecorationCorner,
                ),
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
  WindowResizer({Key key, this.onResized}) : super(key: key);

  final GestureDragUpdateCallback onResized;

  @override
  Widget build(BuildContext context) {
    return new GestureDetector(
      onPanUpdate: onResized,
      child: new Container(
        width: _kWindowDecorationExtent,
        height: _kWindowDecorationExtent,
        decoration: new BoxDecoration(
          borderRadius: const BorderRadius.only(
            bottomRight: _kWindowDecorationCorner,
          ),
          color: _kTitleBarColor,
        ),
      ),
    );
  }
}

class WindowFrame extends StatefulWidget {
  WindowFrame({
    Key key,
    this.initialRect,
    this.title,
    this.child,
  })
      : super(key: key);

  final Rect initialRect;
  final String title;
  final Widget child;

  @override
  _WindowFrameState createState() => new _WindowFrameState();
}

class _WindowFrameState extends State<WindowFrame> {
  @override
  void initState() {
    super.initState();
    if (widget.initialRect != null) {
      _offset = widget.initialRect.topLeft;
      _size = widget.initialRect.size;
    }
  }

  Offset _offset = Offset.zero;
  Size _size = _kInitialWindowSize;

  void _handleResizerDrag(DragUpdateDetails details) {
    setState(() {
      _size = new Size(math.max(0.0, _size.width + details.delta.dx),
          math.max(0.0, _size.height + details.delta.dy));
    });
  }

  void _handleMove(DragUpdateDetails details) {
    setState(() {
      _offset += details.delta;
    });
  }

  void _handleClose() {
    WindowManager.of(context)?.removeWindow(widget);
  }

  void _handleActive() {
    WindowManager.of(context)?.activateWindow(widget);
  }

  @override
  Widget build(BuildContext context) {
    return new Positioned(
      left: _offset.dx,
      top: _offset.dy,
      width: _size.width + _kWindowDecorationExtent * 0.5,
      height: _size.height + _kWindowDecorationExtent,
      child: new Stack(
        children: <Widget>[
          new Positioned(
            right: 0.0,
            bottom: 0.0,
            child: new WindowResizer(onResized: _handleResizerDrag),
          ),
          new Positioned.fill(
            right: _kWindowDecorationExtent * 0.5,
            bottom: _kWindowDecorationExtent * 0.5,
            child: new Column(
              children: <Widget>[
                new WindowTitleBar(
                  title: widget.title,
                  onActivate: _handleActive,
                  onClosed: _handleClose,
                  onMoved: _handleMove,
                ),
                new Expanded(child: widget.child),
              ],
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
    this.initialWindows,
  })
      : super(key: key);

  final Widget wallpaper;
  final List<Widget> initialWindows;

  static WindowManagerState of(BuildContext context) {
    return context.ancestorStateOfType(const TypeMatcher<WindowManagerState>());
  }

  @override
  WindowManagerState createState() => new WindowManagerState();
}

class WindowManagerState extends State<WindowManager> {
  final List<Widget> _windows = <Widget>[];
  final Map<Widget, VoidCallback> _closeCallbacks = <Widget, VoidCallback>{};

  @override
  void initState() {
    super.initState();
    _windows.addAll(widget.initialWindows);
  }

  @override
  void dispose() {
    super.dispose();
    for (VoidCallback callback in _closeCallbacks.values.toList()) {
      if (callback != null)
        callback();
    }
  }

  void addWindow(Widget window, { VoidCallback onClose }) {
    setState(() {
      _windows.add(window);
      if (onClose != null)
        _closeCallbacks[window] = onClose;
    });
  }

  void removeWindow(Widget window) {
    setState(() {
      _windows.remove(window);
    });
    VoidCallback callback = _closeCallbacks.remove(window);
    if (callback != null)
      callback();
  }

  void activateWindow(Widget window) {
    setState(() {
      if (_windows.remove(window))
        _windows.add(window);
    });
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> children = <Widget>[];
    if (widget.wallpaper != null)
      children.add(widget.wallpaper);
    children.addAll(_windows);
    return new Stack(children: children);
  }
}
