// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.app.dart/app.dart';
import 'package:apps.modular.services.story/story_shell.fidl.dart';
import 'package:apps.modular.services.surface/surface.fidl.dart';
import 'package:lib.ui.flutter/child_view.dart';
import 'package:lib.ui.views.fidl/view_token.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/widgets.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();
final GlobalKey<SurfaceLayoutState> _surfaceLayoutKey =
    new GlobalKey<SurfaceLayoutState>();

/// This is used for keeping the reference around.
StoryShellFactoryImpl _storyShellFactory;

void _log(String msg) {
  print('[FlutterStoryShell] $msg');
}

/// Main layout widget for displaying Surfaces.
class SurfaceLayout extends StatefulWidget {
  SurfaceLayout({Key key}) : super(key: key);

  @override
  SurfaceLayoutState createState() => new SurfaceLayoutState();
}

/// Maintains state for the avaialble views to display.
class SurfaceLayoutState extends State<SurfaceLayout> {
  final List<ChildViewConnection> children = <ChildViewConnection>[];

  void addChild(InterfaceHandle<ViewOwner> viewHandle) {
    setState(() {
      children.add(new ChildViewConnection(viewHandle,
          onUnavailable: (ChildViewConnection c) {
        setState(() {
          children.remove(c);
        });
      }));
    });
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> childViews = <Widget>[];
    for (ChildViewConnection conn in children) {
      childViews.add(new Expanded(
          child: new Container(
              margin: const EdgeInsets.all(20.0),
              child: new ChildView(connection: conn))));
    }
    return new Center(child: new Row(children: childViews));
  }
}

/// An implementation of the [StoryShell] interface.
class StoryShellImpl extends StoryShell {
  final StoryShellBinding _storyShellBinding = new StoryShellBinding();
  final StoryContextProxy _storyContext = new StoryContextProxy();

  StoryShellImpl(InterfaceHandle<StoryContext> contextHandle) {
    _storyContext.ctrl.bind(contextHandle);
  }

  /// Bind an [InterfaceRequest] for a [StoryShell] interface to this object.
  void bind(InterfaceRequest<StoryShell> request) {
    _storyShellBinding.bind(this, request);
  }

  /// StoryShell
  @override
  void connectView(InterfaceHandle<ViewOwner> view, String viewId,
      String parentId, SurfaceRelation surfaceRelation) {
    _surfaceLayoutKey.currentState.addChild(view);
  }

  /// StoryShell
  @override
  void focusView(String viewId, String relativeViewId) {
    // Nothing
  }

  /// StoryShell
  @override
  void defocusView(String viewId, void callback()) {
    callback();
  }

  /// StoryShell
  @override
  void terminate() {
    _log('StoryShellImpl::terminate call');
    _storyShellBinding.close();
  }
}

/// An implemenation of the [StoryShellFactory] interface.
class StoryShellFactoryImpl extends StoryShellFactory {
  final StoryShellFactoryBinding _binding = new StoryShellFactoryBinding();
  // ignore: unused_field
  StoryShellImpl _storyShell;

  /// Bind an [InterfaceRequest] for a [StoryShellFactory] interface to this.
  void bind(InterfaceRequest<StoryShellFactory> request) {
    _binding.bind(this, request);
  }

  @override
  void create(InterfaceHandle<StoryContext> context,
      InterfaceRequest<StoryShell> request) {
    _storyShell = new StoryShellImpl(context)..bind(request);
    // TODO(alangardner): Figure out what to do if a second call is made
  }
}

/// Entry point.
void main() {
  _log('Flutter StoryShell started');

  // Note: This implementation only supports one StoryShell at a time.
  // Initialize the one Flutter application we support
  runApp(new SurfaceLayout(key: _surfaceLayoutKey));

  /// Add [ModuleImpl] to this application's outgoing ServiceProvider.
  _appContext.outgoingServices.addServiceForName(
    (request) {
      _log('Received binding request for StoryShellFactory');
      _storyShellFactory = new StoryShellFactoryImpl()..bind(request);
    },
    StoryShellFactory.serviceName,
  );
}
