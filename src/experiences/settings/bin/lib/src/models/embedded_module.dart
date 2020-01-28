// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

/// Holds the name and url to a component that is embedded in the settings app.
class EmbeddedModule {
  /// The name of the component.
  final String name;

  /// The Url to the components's CMX file.
  final String componentUrl;

  ViewHolderToken _viewHolderToken;

  EmbeddedModule({this.name, this.componentUrl});

  /// Returns the [ViewHolderToken] returned after launching the component.
  /// Caches the token after first launch in _viewHolderToken.
  ViewHolderToken get viewHolderToken => _viewHolderToken ??= () {
        final incoming = Incoming();
        final componentController = ComponentControllerProxy();

        final launcher = LauncherProxy();
        StartupContext.fromStartupInfo().incoming.connectToService(launcher);
        launcher.createComponent(
          LaunchInfo(
            url: componentUrl,
            directoryRequest: incoming.request().passChannel(),
          ),
          componentController.ctrl.request(),
        );
        launcher.ctrl.close();

        ViewProviderProxy viewProvider = ViewProviderProxy();
        incoming
          ..connectToService(viewProvider)
          ..close();

        final viewTokens = EventPairPair();
        assert(viewTokens.status == ZX.OK);
        final viewHolderToken = ViewHolderToken(value: viewTokens.first);
        final viewToken = ViewToken(value: viewTokens.second);

        viewProvider.createView(viewToken.value, null, null);
        viewProvider.ctrl.close();

        return viewHolderToken;
      }();
}
