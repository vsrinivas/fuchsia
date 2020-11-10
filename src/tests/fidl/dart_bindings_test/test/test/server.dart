// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

const _kServerName =
    'fuchsia-pkg://fuchsia.com/dart-bindings-test#meta/server.cmx';

class TestServerInstance {
  final TestServerProxy proxy = TestServerProxy();
  final ComponentControllerProxy controller = ComponentControllerProxy();

  Future<void> start() async {
    // Create and connect to a Launcher service
    final launcherProxy = LauncherProxy();
    StartupContext.fromStartupInfo().incoming.connectToService(launcherProxy);

    final incoming = Incoming();
    final launchInfo = LaunchInfo(
        url: _kServerName, directoryRequest: incoming.request().passChannel());
    // Use the launcher services launch echo server via launchInfo
    await launcherProxy.createComponent(launchInfo, controller.ctrl.request());
    // Close connection to launcher service
    launcherProxy.ctrl.close();

    incoming.connectToService(proxy);
  }

  Future<void> stop() async {
    proxy.ctrl.close();
    if (controller.ctrl.isBound) {
      await controller.kill();
    }
  }
}
