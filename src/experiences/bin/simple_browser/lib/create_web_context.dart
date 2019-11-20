// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart' show InterfaceHandle;
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fidl_io;
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:zircon/zircon.dart';

/// Creates a web context for creating new web frames
web.ContextProxy createWebContext() {
  final context = web.ContextProxy();
  final contextProvider = web.ContextProviderProxy();
  final contextProviderProxyRequest = contextProvider.ctrl.request();
  StartupContext.fromStartupInfo().incoming.connectToServiceByNameWithChannel(
      contextProvider.ctrl.$serviceName,
      contextProviderProxyRequest.passChannel());
  final channel = Channel.fromFile('/svc');
  final web.CreateContextParams params = web.CreateContextParams(
      serviceDirectory: InterfaceHandle<fidl_io.Directory>(channel));
  contextProvider.create(params, context.ctrl.request());
  contextProvider.ctrl.close();

  return context;
}
