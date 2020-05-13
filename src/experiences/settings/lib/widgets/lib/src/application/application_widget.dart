// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart' show ViewProviderProxy;
import 'package:fidl_fuchsia_ui_views/fidl_async.dart'
    show ViewToken, ViewHolderToken;
import 'package:flutter/widgets.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:fuchsia_services/services.dart';
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

/// A [Widget] that displays the view of the application it launches.
class ApplicationWidget extends StatefulWidget {
  /// The application to launch.
  final String url;

  // TODO: This class should be responsible to close the launcher connection
  /// The [Launcher] used to launch the application.
  final Launcher launcher;

  /// Called if the application terminates.
  final VoidCallback onDone;

  /// Child can be hit tested.
  final bool hitTestable;

  /// Child can be focused.
  final bool focusable;

  /// Constructor.
  const ApplicationWidget({
    @required this.url,
    @required this.launcher,
    Key key,
    this.onDone,
    this.hitTestable = true,
    this.focusable = true,
  }) : super(key: key);

  @override
  _ApplicationWidgetState createState() => _ApplicationWidgetState();
}

class _ApplicationWidgetState extends State<ApplicationWidget> {
  ComponentControllerProxy _applicationController;
  ChildViewConnection _connection;

  @override
  void initState() {
    super.initState();
    _launchApp();
  }

  @override
  void didUpdateWidget(ApplicationWidget old) {
    super.didUpdateWidget(old);
    if (old.url == widget.url && old.launcher == widget.launcher) {
      return;
    }

    _cleanUp();
    _launchApp();
  }

  @override
  void dispose() {
    _cleanUp();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => ChildView(
        connection: _connection,
        hitTestable: widget.hitTestable,
        focusable: widget.focusable,
      );

  void _cleanUp() {
    _applicationController.ctrl.close();
    _connection = null;
  }

  void _launchApp() {
    _applicationController = ComponentControllerProxy();

    final incomingServices = Incoming();
    widget.launcher.createComponent(
      LaunchInfo(
          url: widget.url,
          directoryRequest: incomingServices.request().passChannel()),
      _applicationController.ctrl.request(),
    );

    _connection = ChildViewConnection(
      _consumeViewProvider(
        _consumeServices(incomingServices),
      ),
      onAvailable: (_) {},
      onUnavailable: (_) => widget.onDone?.call(),
    );
  }

  /// Creates a [ViewProviderProxy] from a [Services], closing it in the
  /// process.
  ViewProviderProxy _consumeServices(Incoming services) {
    ViewProviderProxy viewProvider = ViewProviderProxy();
    services
      ..connectToService(viewProvider)
      ..close();
    return viewProvider;
  }

  /// Creates a handle to a [ViewHolderToken] from a [ViewProviderProxy],
  /// closing it in the process.
  ViewHolderToken _consumeViewProvider(
    ViewProviderProxy viewProvider,
  ) {
    final viewTokens = EventPairPair();
    assert(viewTokens.status == ZX.OK);
    final viewHolderToken = ViewHolderToken(value: viewTokens.first);
    final viewToken = ViewToken(value: viewTokens.second);

    viewProvider.createView(viewToken.value, null, null);
    viewProvider.ctrl.close();
    return viewHolderToken;
  }
}
