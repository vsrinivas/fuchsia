// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/client.fidl.dart';
import 'package:apps.maxwell.services.context/publisher_link.fidl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:meta/meta.dart';

import 'context_publisher_controller_impl.dart';

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

typedef void PublishFn(
    String label,
    InterfaceHandle<ContextPublisherController> controller,
    InterfaceRequest<ContextPublisherLink> link);

typedef void SubscribeFn(
    String label, InterfaceHandle<ContextSubscriberLink> link);

/// Registers a publisher link using the globally bound [ContextPublisher].
PublishFn get publish => _contextPublisher.publish;

/// Registers a subscriber link using the globally bound [ContextSubscriber].
SubscribeFn get subscribe => _contextSubscriber.subscribe;

/// Convenience function that creates a [ContextPublisherLinkProxy] with the
/// given label. The controller of the returned proxy should be closed once
/// unneeded.
ContextPublisherLinkProxy publisherLink(String label,
    {Function onHasSubscribers, Function onNoSubscribers}) {
  final pub = new ContextPublisherLinkProxy();

  ContextPublisherControllerImpl ctrl;
  if (onHasSubscribers != null || onNoSubscribers != null) {
    ctrl =
        new ContextPublisherControllerImpl(onHasSubscribers, onNoSubscribers);
    // TODO(rosswang): Do we need this?
    pub.ctrl.error.then((_) => ctrl.close());
  }

  publish(label, ctrl?.getHandle(), pub.ctrl.request());
  return pub;
}

/// Convenience function that subscribes to a label with a handler callback.
/// The returned [ContextSubscriberLinkImpl] should be closed once unneeded.
ContextSubscriberLinkImpl subscriberLink(
    String label, Function handler) {
  final sub = new ContextSubscriberLinkImpl(handler);
  subscribe(label, sub.getHandle());
  return sub;
}

typedef Future<String> TransformFn(String);

/// Convenience function that sets up a transformation pipeline between two
/// pieces of context.
///
/// This pipeline will tear down the subscriber link if there are no downstream
/// subscribers and set it up when there are. To do so, the global context
/// client must remain open, so [closeGlobals] should not be called until the
/// transform pipeline is no longer necessary (or more accurately, until it no
/// longer needs to be resurrected).
ContextPublisherLinkProxy buildTransform(
    {@required String labelIn,
    @required String schemaIn,
    @required String labelOut,
    @required String schemaOut,
    @required Function transform,
    bool invalidateOnNoSubscribers = true,
    bool invalidateOnNull = true}) {
  ContextSubscriberLinkImpl sub;
  ContextPublisherLinkProxy pub;

  pub = publisherLink(labelOut, schemaOut, onHasSubscribers: () {
    sub = subscriberLink(labelIn, schemaIn, (update) {
      if (update == null) {
        pub.update(null);
      } else {
        transform(update).then(pub.update);
      }
    });
  }, onNoSubscribers: () {
    if (invalidateOnNoSubscribers) pub.update(null);
    sub.close();
  });

  // TODO(rosswang): Do we need this?
  pub.ctrl.error.then((_) {
    sub.close();
  });
  return pub;
}

/// Closes any bound global FIDL handles. This should be called on app cleanup.
void closeGlobals() {
  _contextPublisher.ctrl.close();
  _contextSubscriber.ctrl.close();
}
