// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/context_publisher.fidl.dart';
import 'package:apps.maxwell.services.context/context_subscriber.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:meta/meta.dart';

final _contextPublisher = new ContextPublisherProxy();
final _contextSubscriber = new ContextSubscriberProxy();

/// Connects to the environment's [ContextPublisher]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectPublisher(ApplicationContext appContext) {
  connectToService(appContext.environmentServices, _contextPublisher.ctrl);
  assert(_contextPublisher.ctrl.isBound);
}

/// Connects to the environment's [ContextSubscriber]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectSubscriber(ApplicationContext appContext) {
  connectToService(appContext.environmentServices, _contextSubscriber.ctrl);
  assert(_contextSubscriber.ctrl.isBound);
}

typedef void PublishFn(String topic, String json_value);

typedef void SubscribeFn(
    ContextQuery query, InterfaceHandle<ContextListener> listener);

/// Publish a value using the globally bound [ContextPublisher].
PublishFn get publish => _contextPublisher.publish;

/// Registers a subscriber link using the globally bound [ContextSubscriber].
SubscribeFn get subscribe => _contextSubscriber.subscribe;

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
  _contextSubscriber.ctrl.close();
}
