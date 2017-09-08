// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:mozart.internal';
import 'dart:ui' as ui;
import 'dart:zircon';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/application_controller.fidl.dart';
import 'package:application.services/application_launcher.fidl.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:lib.ui.geometry.fidl/geometry.fidl.dart' as fidl;
import 'package:lib.ui.views.fidl/view_containers.fidl.dart';
import 'package:lib.ui.views.fidl/view_properties.fidl.dart';
import 'package:lib.ui.views.fidl/view_provider.fidl.dart';
import 'package:lib.ui.views.fidl/view_token.fidl.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:meta/meta.dart';

export 'package:lib.ui.views.fidl/view_token.fidl.dart' show ViewOwner;

ViewContainerProxy _initViewContainer() {
  final Handle handle = MozartStartupInfo.takeViewContainer();
  if (handle == null) return null;
  final ViewContainerProxy proxy = new ViewContainerProxy()
    ..ctrl.bind(new InterfaceHandle<ViewContainer>(new Channel(handle), 0))
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
          childServices: childServices,
          onAvailable: onAvailable,
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

  ViewProperties _currentViewProperties;

  VoidCallback _onViewInfoAvailable;
  ViewInfo _viewInfo;
  ui.SceneHost _sceneHost;

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
    assert(_sceneHost == null);
    final HandlePairResult pair = System.eventpairCreate();
    assert(pair.status == ZX.OK);
    _sceneHost = new ui.SceneHost(pair.first);
    _viewKey = _nextViewKey++;
    _viewContainer.addChild(_viewKey, _viewOwner, pair.second);
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
    assert(_sceneHost != null);
    assert(_ViewContainerListenerImpl.instance._connections[_viewKey] == this);
    final ChannelPair pair = new ChannelPair();
    assert(pair.status == ZX.OK);
    _ViewContainerListenerImpl.instance._connections.remove(_viewKey);
    _viewOwner = new InterfaceHandle<ViewOwner>(pair.first, 0);
    _viewContainer.removeChild(
        _viewKey, new InterfaceRequest<ViewOwner>(pair.second));
    _viewKey = null;
    _viewInfo = null;
    _currentViewProperties = null;
    _sceneHost.dispose();
    _sceneHost = null;
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
      double width,
      double height,
      double devicePixelRatio,
      double insetTop,
      double insetRight,
      double insetBottom,
      double insetLeft) {
    if (_currentViewProperties != null &&
        _currentViewProperties.displayMetrics.devicePixelRatio ==
            devicePixelRatio &&
        _currentViewProperties.viewLayout.size.width == width &&
        _currentViewProperties.viewLayout.size.height == height &&
        _currentViewProperties.viewLayout.inset.top == insetTop &&
        _currentViewProperties.viewLayout.inset.right == insetRight &&
        _currentViewProperties.viewLayout.inset.bottom == insetBottom &&
        _currentViewProperties.viewLayout.inset.left == insetLeft) return null;

    DisplayMetrics displayMetrics = new DisplayMetrics()
      ..devicePixelRatio = devicePixelRatio;
    fidl.SizeF size = new fidl.SizeF()
      ..width = width
      ..height = height;
    fidl.InsetF inset = new fidl.InsetF()
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
      double width,
      double height,
      double devicePixelRatio,
      double insetTop,
      double insetRight,
      double insetBottom,
      double insetLeft) {
    assert(_attached);
    assert(_attachments == 1);
    assert(_viewKey != null);
    if (_viewContainer == null) return;
    ViewProperties viewProperties = _createViewProperties(width, height,
        devicePixelRatio, insetTop, insetRight, insetBottom, insetLeft);
    if (viewProperties == null) return;
    _viewContainer.setChildProperties(_viewKey, viewProperties);
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

  double _width;
  double _height;

  @override
  void performLayout() {
    size = constraints.biggest;
    if (_connection != null) {
      _width = size.width;
      _height = size.height;
      _connection._setChildProperties(
          _width, _height, scale, 0.0, 0.0, 0.0, 0.0);
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
        width: _width,
        height: _height,
        sceneHost: _connection._sceneHost,
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
  void debugFillProperties(DiagnosticPropertiesBuilder description) {
    super.debugFillProperties(description);
    description.add(
      new DiagnosticsProperty<ChildViewConnection>(
        'connection',
        connection,
      ),
    );
    description.add(new DoubleProperty('scale', scale));
  }
}

/// A layer that represents content from another process.
class ChildSceneLayer extends Layer {
  /// Creates a layer that displays content rendered by another process.
  ///
  /// All of the arguments must not be null.
  ChildSceneLayer({
    this.offset: Offset.zero,
    this.width: 0.0,
    this.height: 0.0,
    this.sceneHost,
    this.hitTestable: true,
  });

  /// Offset from parent in the parent's coordinate system.
  Offset offset;

  /// The horizontal extent of the child, in logical pixels.
  double width;

  /// The vertical extent of the child, in logical pixels.
  double height;

  /// The host site for content rendered by the child.
  ui.SceneHost sceneHost;

  /// Whether this child should be included during hit testing.
  ///
  /// Defaults to true.
  bool hitTestable;

  @override
  void addToScene(ui.SceneBuilder builder, Offset layerOffset) {
    builder.addChildScene(
      offset: offset + layerOffset,
      width: width,
      height: height,
      sceneHost: sceneHost,
      hitTestable: hitTestable,
    );
  }

  @override
  void debugFillProperties(DiagnosticPropertiesBuilder description) {
    super.debugFillProperties(description);
    description.add(new DiagnosticsProperty<Offset>('offset', offset));
    description.add(new DoubleProperty('width', width));
    description.add(new DoubleProperty('height', height));
    description.add(new DiagnosticsProperty<ui.SceneHost>('sceneHost', sceneHost));
    description.add(new DiagnosticsProperty<bool>('hitTestable', hitTestable));
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
  /// Provide services to Mozart throught |provider|.
  ///
  /// |services| should contain the list of service names offered by the
  /// |provider|.
  static void offerServiceProvider(
      InterfaceHandle<ServiceProvider> provider, List<String> services) {
    Mozart.offerServiceProvider(provider.passChannel().handle, services);
  }
}
