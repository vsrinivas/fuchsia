// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:mozart.internal';
import 'dart:ui' as ui;

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/application_controller.fidl.dart';
import 'package:application.services/application_launcher.fidl.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.mozart.services.geometry/geometry.fidl.dart' as fidl;
import 'package:apps.mozart.services.views/view_containers.fidl.dart';
import 'package:apps.mozart.services.views/view_properties.fidl.dart';
import 'package:apps.mozart.services.views/view_provider.fidl.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:lib.fidl.dart/core.dart' as core;
import 'package:meta/meta.dart';

export 'package:apps.mozart.services.views/view_token.fidl.dart' show ViewOwner;

ViewContainerProxy _initViewContainer() {
  final int viewContainerHandle = MozartStartupInfo.takeViewContainer();
  if (viewContainerHandle == null) return null;
  final core.Handle handle = new core.Handle(viewContainerHandle);
  final ViewContainerProxy proxy = new ViewContainerProxy()
    ..ctrl.bind(new InterfaceHandle<ViewContainer>(new core.Channel(handle), 0))
    ..setListener(_ViewContainerListenerImpl.instance.createInterfaceHandle());

  assert(() {
    proxy.ctrl.error.then((ProxyError error) {
      print('ViewContainerProxy: error: $error');
    });
    return true;
  });

  return proxy;
}

final ViewContainerProxy _viewContainer = _initViewContainer();

class _ViewContainerListenerImpl extends ViewContainerListener {
  final ViewContainerListenerBinding _binding =
      new ViewContainerListenerBinding();

  InterfaceHandle<ViewContainerListener> createInterfaceHandle() {
    return _binding.wrap(this);
  }

  static final _ViewContainerListenerImpl instance =
      new _ViewContainerListenerImpl();

  @override
  void onChildAttached(int childKey, ViewInfo childViewInfo, void callback()) {
    ChildViewConnection connection = _connections[childKey];
    connection?._onAttachedToContainer(childViewInfo);
    callback();
  }

  @override
  void onChildUnavailable(int childKey, void callback()) {
    ChildViewConnection connection = _connections[childKey];
    connection?._onUnavailable();
    callback();
  }

  final Map<int, ChildViewConnection> _connections =
      new HashMap<int, ChildViewConnection>();
}

typedef void ChildViewConnectionCallback(ChildViewConnection connection);
final ChildViewConnectionCallback _emptyConnectionCallback =
    (ChildViewConnection c) {};

/// A connection with a child view.
///
/// Used with the [ChildView] widget to display a child view.
class ChildViewConnection {
  ChildViewConnection(this._viewOwner,
      {ChildViewConnectionCallback onAvailable,
      ChildViewConnectionCallback onUnavailable})
      : _onAvailableCallback = onAvailable ?? _emptyConnectionCallback,
        _onUnavailableCallback = onUnavailable ?? _emptyConnectionCallback {
    assert(_viewOwner != null);
  }

  factory ChildViewConnection.launch(String url, ApplicationLauncher launcher,
      {InterfaceRequest<ApplicationController> controller,
      InterfaceRequest<ServiceProvider> childServices,
      ChildViewConnectionCallback onAvailable,
      ChildViewConnectionCallback onUnavailable}) {
    final ServiceProviderProxy services = new ServiceProviderProxy();
    final ApplicationLaunchInfo launchInfo = new ApplicationLaunchInfo()
      ..url = url
      ..services = services.ctrl.request();
    try {
      launcher.createApplication(launchInfo, controller);
      return new ChildViewConnection.connect(services,
          childServices: childServices, onAvailable: onAvailable,
          onUnavailable: onUnavailable);
    } finally {
      services.ctrl.close();
    }
  }

  factory ChildViewConnection.connect(ServiceProvider services,
      {InterfaceRequest<ServiceProvider> childServices,
      ChildViewConnectionCallback onAvailable,
      ChildViewConnectionCallback onUnavailable}) {
    final ViewProviderProxy viewProvider = new ViewProviderProxy();
    connectToService(services, viewProvider.ctrl);
    try {
      final InterfacePair<ViewOwner> viewOwner = new InterfacePair<ViewOwner>();
      viewProvider.createView(viewOwner.passRequest(), childServices);
      return new ChildViewConnection(viewOwner.passHandle(),
          onAvailable: onAvailable, onUnavailable: onUnavailable);
    } finally {
      viewProvider.ctrl.close();
    }
  }

  final ChildViewConnectionCallback _onAvailableCallback;
  final ChildViewConnectionCallback _onUnavailableCallback;
  InterfaceHandle<ViewOwner> _viewOwner;


  static int _nextViewKey = 1;
  int _viewKey;

  int _sceneVersion = 1;
  ViewProperties _currentViewProperties;

  VoidCallback _onViewInfoAvailable;
  ViewInfo _viewInfo;

  void _onAttachedToContainer(ViewInfo viewInfo) {
    assert(_viewInfo == null);
    _viewInfo = viewInfo;
    if (_onViewInfoAvailable != null) _onViewInfoAvailable();
    _onAvailableCallback(this);
  }

  void _onUnavailable() {
    _viewInfo = null;
    _onUnavailableCallback(this);
  }

  void _addChildToViewHost() {
    if (_viewContainer == null) return;
    assert(_attached);
    assert(_viewOwner != null);
    assert(_viewKey == null);
    assert(_viewInfo == null);
    _viewKey = _nextViewKey++;
    _viewContainer.addChild(_viewKey, _viewOwner);
    _viewOwner = null;
    assert(!_ViewContainerListenerImpl.instance._connections
        .containsKey(_viewKey));
    _ViewContainerListenerImpl.instance._connections[_viewKey] = this;
  }

  void _removeChildFromViewHost() {
    if (_viewContainer == null) return;
    assert(!_attached);
    assert(_viewOwner == null);
    assert(_viewKey != null);
    assert(_ViewContainerListenerImpl.instance._connections[_viewKey] == this);
    final core.ChannelPair pair = new core.ChannelPair();
    _ViewContainerListenerImpl.instance._connections.remove(_viewKey);
    _viewOwner = new InterfaceHandle<ViewOwner>(pair.channel0, 0);
    _viewContainer.removeChild(
        _viewKey, new InterfaceRequest<ViewOwner>(pair.channel1));
    _viewKey = null;
    _viewInfo = null;
    _currentViewProperties = null;
  }

  // The number of render objects attached to this view. In between frames, we
  // might have more than one connected if we get added to a new render object
  // before we get removed from the old render object. By the time we get around
  // to computing our layout, we must be back to just having one render object.
  int _attachments = 0;
  bool get _attached => _attachments > 0;

  void _attach() {
    assert(_attachments >= 0);
    ++_attachments;
    if (_viewKey == null) _addChildToViewHost();
  }

  void _detach() {
    assert(_attached);
    --_attachments;
    scheduleMicrotask(_removeChildFromViewHostIfNeeded);
  }

  void _removeChildFromViewHostIfNeeded() {
    assert(_attachments >= 0);
    if (_attachments == 0) _removeChildFromViewHost();
  }

  ViewProperties _createViewProperties(
      int physicalWidth, int physicalHeight, double devicePixelRatio,
      int insetTop, int insetRight, int insetBottom, int insetLeft) {
    if (_currentViewProperties != null &&
        _currentViewProperties.displayMetrics.devicePixelRatio ==
            devicePixelRatio &&
        _currentViewProperties.viewLayout.size.width == physicalWidth &&
        _currentViewProperties.viewLayout.size.height == physicalHeight &&
        _currentViewProperties.viewLayout.inset.top == insetTop &&
        _currentViewProperties.viewLayout.inset.right == insetRight &&
        _currentViewProperties.viewLayout.inset.bottom == insetBottom &&
        _currentViewProperties.viewLayout.inset.left == insetLeft)
      return null;

    DisplayMetrics displayMetrics = new DisplayMetrics()
      ..devicePixelRatio = devicePixelRatio;
    fidl.Size size = new fidl.Size()
      ..width = physicalWidth
      ..height = physicalHeight;
    fidl.Inset inset = new fidl.Inset()
      ..top = insetTop
      ..right = insetRight
      ..bottom = insetBottom
      ..left = insetLeft;
    ViewLayout viewLayout = new ViewLayout()
      ..size = size
      ..inset = inset;
    _currentViewProperties = new ViewProperties()
      ..displayMetrics = displayMetrics
      ..viewLayout = viewLayout;
    return _currentViewProperties;
  }

  void _setChildProperties(
      int physicalWidth, int physicalHeight, double devicePixelRatio,
      int insetTop, int insetRight, int insetBottom, int insetLeft) {
    assert(_attached);
    assert(_attachments == 1);
    assert(_viewKey != null);
    if (_viewContainer == null) return;
    ViewProperties viewProperties =
        _createViewProperties(physicalWidth, physicalHeight, devicePixelRatio,
        insetTop, insetRight, insetBottom, insetLeft);
    if (viewProperties == null) return;
    _viewContainer.setChildProperties(
        _viewKey, _sceneVersion++, viewProperties);
  }
}

class _RenderChildView extends RenderBox {
  /// Creates a child view render object.
  ///
  /// The [scale] argument must not be null.
  _RenderChildView({
    ChildViewConnection connection,
    double scale,
    bool hitTestable: true,
  })
      : _scale = scale,
        _hitTestable = hitTestable {
    assert(scale != null);
    assert(hitTestable != null);
    this.connection = connection;
  }

  /// The child to display.
  ChildViewConnection get connection => _connection;
  ChildViewConnection _connection;
  set connection(ChildViewConnection value) {
    if (value == _connection) return;
    if (attached && _connection != null) {
      _connection._detach();
      assert(_connection._onViewInfoAvailable != null);
      _connection._onViewInfoAvailable = null;
    }
    _connection = value;
    if (attached && _connection != null) {
      _connection._attach();
      assert(_connection._onViewInfoAvailable == null);
      _connection._onViewInfoAvailable = markNeedsPaint;
    }
    if (_connection == null) {
      markNeedsPaint();
    } else {
      markNeedsLayout();
    }
  }

  /// The device pixel ratio to provide the child.
  double get scale => _scale;
  double _scale;
  set scale(double value) {
    assert(value != null);
    if (value == _scale) return;
    _scale = value;
    if (_connection != null) markNeedsLayout();
  }

  /// Whether this child should be included during hit testing.
  bool get hitTestable => _hitTestable;
  bool _hitTestable;
  set hitTestable(bool value) {
    assert(value != null);
    if (value == _hitTestable) return;
    _hitTestable = value;
    if (_connection != null) markNeedsPaint();
  }

  @override
  void attach(PipelineOwner owner) {
    super.attach(owner);
    if (_connection != null) {
      _connection._attach();
      assert(_connection._onViewInfoAvailable == null);
      _connection._onViewInfoAvailable = markNeedsPaint;
    }
  }

  @override
  void detach() {
    if (_connection != null) {
      _connection._detach();
      assert(_connection._onViewInfoAvailable != null);
      _connection._onViewInfoAvailable = null;
    }
    super.detach();
  }

  @override
  bool get alwaysNeedsCompositing => true;

  TextPainter _debugErrorMessage;

  int _physicalWidth;
  int _physicalHeight;

  @override
  void performLayout() {
    size = constraints.biggest;
    if (_connection != null) {
      _physicalWidth = (size.width * scale).round();
      _physicalHeight = (size.height * scale).round();
      _connection._setChildProperties(_physicalWidth, _physicalHeight, scale,
        0, 0, 0, 0);
      assert(() {
        if (_viewContainer == null) {
          _debugErrorMessage ??= new TextPainter(
              text: new TextSpan(
                  text:
                      'Child views are supported only when running in Mozart.'));
          _debugErrorMessage.layout(minWidth: size.width, maxWidth: size.width);
        }
        return true;
      });
    }
  }

  @override
  bool hitTestSelf(Offset position) => true;

  @override
  void paint(PaintingContext context, Offset offset) {
    assert(needsCompositing);
    if (_connection?._viewInfo != null) {
      context.addLayer(new ChildSceneLayer(
        offset: offset,
        devicePixelRatio: scale,
        physicalWidth: _physicalWidth,
        physicalHeight: _physicalHeight,
        sceneToken: _connection._viewInfo.sceneToken.value,
        hitTestable: hitTestable,
      ));
    }
    assert(() {
      if (_viewContainer == null) {
        context.canvas.drawRect(
            offset & size, new Paint()..color = const Color(0xFF0000FF));
        _debugErrorMessage.paint(context.canvas, offset);
      }
      return true;
    });
  }

  @override
  void debugFillDescription(List<String> description) {
    super.debugFillDescription(description);
    description.add('connection: $connection');
    description.add('scale: $scale');
  }
}

/// A layer that represents content from another process.
class ChildSceneLayer extends Layer {
  /// Creates a layer that displays content rendered by another process.
  ///
  /// All of the arguments must not be null.
  ChildSceneLayer({
    this.offset: Offset.zero,
    this.devicePixelRatio: 1.0,
    this.physicalWidth: 0,
    this.physicalHeight: 0,
    this.sceneToken: 0,
    this.hitTestable: true,
  });

  /// Offset from parent in the parent's coordinate system.
  Offset offset;

  /// The number of physical pixels the child should produce for each logical pixel.
  double devicePixelRatio;

  /// The horizontal extent of the child, in physical pixels.
  int physicalWidth;

  /// The vertical extent of the child, in physical pixels.
  int physicalHeight;

  /// The composited scene that will contain the content rendered by the child.
  int sceneToken;

  /// Whether this child should be included during hit testing.
  ///
  /// Defaults to true.
  bool hitTestable;

  @override
  void addToScene(ui.SceneBuilder builder, Offset layerOffset) {
    builder.addChildScene(
      offset: offset + layerOffset,
      devicePixelRatio: devicePixelRatio,
      physicalWidth: physicalWidth,
      physicalHeight: physicalHeight,
      sceneToken: sceneToken,
      hitTestable: hitTestable,
    );
  }

  @override
  void debugFillDescription(List<String> description) {
    super.debugFillDescription(description);
    description.add('offset: $offset');
    description.add('physicalWidth: $physicalWidth');
    description.add('physicalHeight: $physicalHeight');
    description.add('sceneToken: $sceneToken');
  }
}

/// A widget that is replaced by content from another process.
///
/// Requires a [MediaQuery] ancestor to provide appropriate media information to
/// the child.
@immutable
class ChildView extends LeafRenderObjectWidget {
  /// Creates a widget that is replaced by content from another process.
  ChildView({ChildViewConnection connection, this.hitTestable: true})
      : connection = connection,
        super(key: new GlobalObjectKey(connection));

  /// A connection to the child whose content will replace this widget.
  final ChildViewConnection connection;

  /// Whether this child should be included during hit testing.
  ///
  /// Defaults to true.
  final bool hitTestable;

  @override
  _RenderChildView createRenderObject(BuildContext context) {
    return new _RenderChildView(
      connection: connection,
      scale: MediaQuery.of(context).devicePixelRatio,
      hitTestable: hitTestable,
    );
  }

  @override
  void updateRenderObject(BuildContext context, _RenderChildView renderObject) {
    renderObject
      ..connection = connection
      ..scale = MediaQuery.of(context).devicePixelRatio
      ..hitTestable = hitTestable;
  }
}

class View {
  static void offerServiceProvider(InterfaceHandle<ServiceProvider> provider) {
    Mozart.offerServiceProvider(provider.passChannel().handle.release());
  }
}
