// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/context_listener_impl.dart';
import 'package:apps.maxwell.services.context/context_publisher.fidl.dart';
import 'package:apps.maxwell.services.context/context_provider.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:meta/meta.dart';

final _contextPublisher = new ContextPublisherProxy();
final _contextProvider = new ContextProviderProxy();

/// Connects to the environment's [ContextPublisher]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectPublisher(ApplicationContext appContext) {
  connectToService(appContext.environmentServices, _contextPublisher.ctrl);
  assert(_contextPublisher.ctrl.isBound);
}

/// Connects to the environment's [ContextProvider]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectProvider(ApplicationContext appContext) {
  connectToService(appContext.environmentServices, _contextProvider.ctrl);
  assert(_contextProvider.ctrl.isBound);
}

typedef void PublishFn(String topic, String json_value);

typedef void SubscribeFn(
    ContextQuery query, InterfaceHandle<ContextListener> listener);

/// Publish a value using the globally bound [ContextPublisher].
PublishFn get publish => _contextPublisher.publish;

/// Registers a [ContextListener] using the globally bound [ContextProvider].
SubscribeFn get subscribe => _contextProvider.subscribe;

/// Convenience function that subscribes to a query with a handler callback.
/// The returned [ContextListenerImpl] should be closed once unneeded.
ContextListenerImpl subscribe(
    ContextQuery query, Function handler) {
  final listener = new ContextListenerImpl(handler);
  subscribe(query, listener.getHandle());
  return listener;
}

/// Closes any bound global FIDL handles. This should be called on app cleanup.
void closeGlobals() {
  _contextPublisher.ctrl.close();
  _contextProvider.ctrl.close();
}
