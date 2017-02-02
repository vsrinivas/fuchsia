// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/client.fidl.dart';
import 'package:apps.maxwell.services.context/publisher_link.fidl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

final ContextPublisherProxy _contextPublisher = new ContextPublisherProxy();
final ContextSubscriberProxy _contextSubscriber = new ContextSubscriberProxy();

/// Connects to the environment's [ContextPublisher]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectPublisher(ApplicationContext appContext) =>
    connectToService(appContext.environmentServices, _contextPublisher.ctrl);

/// Connects to the environment's [ContextSubscriber]. This should typically be
/// called from an app's [main] function, accompanied by a call to
/// [closeGlobals] prior to shutdown.
void connectSubscriber(ApplicationContext appContext) =>
    connectToService(appContext.environmentServices, _contextSubscriber.ctrl);

// TODO(rosswang): pubsub proxy

typedef void PublishFn(
    String label,
    String schema,
    InterfaceHandle<ContextPublisherController> controller,
    InterfaceRequest<ContextPublisherLink> link);

typedef void SubscribeFn(
    String label, String schema, InterfaceHandle<ContextSubscriberLink> link);

/// Registers a publisher link using the globally bound [ContextPubSub] or
/// [ContextPublisher].
PublishFn get publish => _contextPublisher.publish;

/// Registers a subscriber link using the globally bound [ContextPubSub] or
/// [ContextSubscriber].
SubscribeFn get subscribe => _contextSubscriber.subscribe;

/// Convenience function that creates a [ContextPublisherLinkProxy] with the
/// given label and schema. The controller of the returned proxy should be
/// closed once unneeded.
ContextPublisherLinkProxy publisherLink(String label, String schema) {
  final pub = new ContextPublisherLinkProxy();
  _contextPublisher.publish(label, schema, null, pub.ctrl.request());
  return pub;
}

/// Convenience function that subscribes to a label and schema with a handler
/// callback. The returned [ContextSubscriberLinkImpl] should be closed once
/// unneeded.
ContextSubscriberLinkImpl subscriberLink(
    String label, String schema, Function handler) {
  final sub = new ContextSubscriberLinkImpl(handler);
  subscribe(label, schema, sub.getHandle());
  return sub;
}

/// Closes any bound global FIDL handles. This should be called on app cleanup.
void closeGlobals() {
  _contextPublisher.ctrl.close();
  _contextSubscriber.ctrl.close();
}
