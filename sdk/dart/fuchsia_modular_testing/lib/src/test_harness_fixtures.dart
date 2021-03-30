// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl_modular;
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart' as fidl_testing;
import 'package:fidl_fuchsia_sys/fidl_async.dart' as fidl_sys;
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

const _serviceRootPath = '/svc';
const _modularTestHarnessURL =
    'fuchsia-pkg://fuchsia.com/modular_test_harness#meta/modular_test_harness.cmx';

/// Launches the test harness and connects to it.
///
/// It is the responsibility of the caller to close the connection to the
/// harness after each test.
Future<fidl_testing.TestHarnessProxy> launchTestHarness() async {
  final harness = fidl_testing.TestHarnessProxy();
  final launcher = fidl_sys.LauncherProxy();
  final incoming = Incoming();

  final componentControllerProxy = fidl_sys.ComponentControllerProxy();

  final svc = Incoming.fromSvcPath()..connectToService(launcher);
  final launchInfo = fidl_sys.LaunchInfo(
      url: _modularTestHarnessURL,
      directoryRequest: incoming.request().passChannel());
  await launcher.createComponent(
      launchInfo, componentControllerProxy.ctrl.request());
  launcher.ctrl.close();
  await svc.close();

  // hold a reference to the componentControllerProxy so it lives as long as the
  // harness and does not kill the service if it is garbage collected.
  // ignore: unawaited_futures
  harness.ctrl.whenClosed.then((_) {
    componentControllerProxy.ctrl.close();
  });

  incoming.connectToService(harness);
  await incoming.close();

  return harness;
}

/// Generates a random component url with the correct format
String generateComponentUrl() {
  final rand = Random();
  final name = List.generate(10, (_) => rand.nextInt(9).toString()).join('');
  return 'fuchsia-pkg://example.com/$name#meta/$name.cmx';
}

/// Returns the connection to [fidl_modular.ComponentContextProxy] which is
/// running inside the [harness]'s hermetic environment
Future<fidl_modular.ComponentContextProxy> getComponentContext(
    fidl_testing.TestHarnessProxy harness) async {
  final proxy = fidl_modular.ComponentContextProxy();
  await harness.connectToModularService(
      fidl_testing.ModularService.withComponentContext(proxy.ctrl.request()));
  return proxy;
}

/// Creates an instance of [ComponentContext] from [fidl_sys.StartupInfo].
///
/// Note: This automatically serves [ComponentContext.outgoing].
ComponentContext createComponentContext(fidl_sys.StartupInfo startupInfo) {
  final flat = startupInfo.flatNamespace;
  if (flat.paths.length != flat.directories.length) {
    throw Exception('The flat namespace in the given fuchsia.sys.StartupInfo '
        '[$startupInfo] is misconfigured');
  }
  Channel? serviceRoot;
  for (var i = 0; i < flat.paths.length; ++i) {
    if (flat.paths[i] == _serviceRootPath) {
      serviceRoot = flat.directories[i];
      break;
    }
  }
  final svcDirProxy = DirectoryProxy()..ctrl.bind(InterfaceHandle(serviceRoot));

  return ComponentContext(
    svc: Incoming.withDirectory(svcDirProxy),
    outgoing: Outgoing()
      ..serve(InterfaceRequest<Node>(startupInfo.launchInfo.directoryRequest)),
  );
}
